#include "CommandProcessor.hpp"
#include "DebugLog.hpp"
#include "New3DTracker.hpp"
#include "../src/modules/UdpBallColorModule.hpp"
#include "../src/modules/PositionToRgbModule.hpp"
#include "../src/modules/UdpBallSettingsModule.hpp"
#include <thread>
#include <chrono>

extern void writeDebugLog(const std::string& message);

CommandProcessor::CommandProcessor(zmq::context_t& zmq_context)
    : zmq_commander_(zmq_context, ZMQ_REP),
      camera_manager_(nullptr),
      recording_manager_(nullptr),
      playback_controller_(nullptr),
      color_module_(nullptr),
      settings_module_(nullptr),
      visualization_states_(nullptr),
      record_with_yolo_boxes_(nullptr),
      video_feed_enabled_(nullptr),
      current_tracker_type_(nullptr),
      running_(false) {
    
    writeDebugLog("CommandProcessor constructor: Binding ZMQ commander socket...");
    zmq_commander_.bind("tcp://127.0.0.1:5565");
    writeDebugLog("CommandProcessor constructor: ZMQ commander socket bound successfully");
}

CommandProcessor::~CommandProcessor() {
    stop();
}

void CommandProcessor::start() {
    running_ = true;
}

void CommandProcessor::stop() {
    running_ = false;
}

void CommandProcessor::sendCommand(const juggler::v1::CommandRequest& command) {
    std::lock_guard<std::mutex> lock(command_queue_mutex_);
    command_queue_.push(command);
}

void CommandProcessor::processCommands() {
    while (running_) {
        // Handle external ZMQ commands
        zmq::message_t request;
        auto result = zmq_commander_.recv(request, zmq::recv_flags::dontwait);
        
        if (result) {
            juggler::v1::CommandRequest command;
            command.ParseFromArray(request.data(), request.size());
            
            juggler::v1::CommandResponse response;
            response.set_success(true);
            
            handleExternalCommand(command, response);
            
            // Send response back via ZMQ
            std::string serialized_response;
            response.SerializeToString(&serialized_response);
            zmq::message_t reply(serialized_response.size());
            memcpy(reply.data(), serialized_response.c_str(), serialized_response.size());
            zmq_commander_.send(reply, zmq::send_flags::none);
        }

        // Handle internal commands from modules
        juggler::v1::CommandRequest internal_command;
        bool command_found = false;
        {
            std::lock_guard<std::mutex> lock(command_queue_mutex_);
            if (!command_queue_.empty()) {
                internal_command = command_queue_.front();
                command_queue_.pop();
                command_found = true;
            }
        }

        if (command_found) {
            handleInternalCommand(internal_command);
        }

        if (!result && !command_found) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void CommandProcessor::handleExternalCommand(const juggler::v1::CommandRequest& command,
                                            juggler::v1::CommandResponse& response) {
    switch (command.type()) {
        case juggler::v1::CommandRequest::LOAD_MODULE:
            active_module_ = createModule(command);
            if (active_module_) {
                active_module_->setup();
                response.set_message(command.module_name() + " loaded");
            } else {
                response.set_success(false);
                response.set_message("Unknown module: " + command.module_name());
            }
            break;
            
        case juggler::v1::CommandRequest::UNLOAD_MODULE:
            if (active_module_) {
                active_module_->cleanup();
                active_module_.reset();
                response.set_message("Module unloaded");
            } else {
                response.set_success(false);
                response.set_message("No active module");
            }
            break;
            
        case juggler::v1::CommandRequest::CONFIGURE_MODULE:
            if (active_module_) {
                active_module_->processCommand(command);
                response.set_message("Module configuration sent to " + command.module_name());
            } else {
                response.set_success(false);
                response.set_message("No active module to configure.");
            }
            break;
            
        case juggler::v1::CommandRequest::RECORD_START:
            writeDebugLog("CommandProcessor - RECORD_START command received");
            if (record_with_yolo_boxes_) {
                *record_with_yolo_boxes_ = command.record_with_yolo_boxes();
            }
            if (command.has_visualization_states() && visualization_states_) {
                *visualization_states_ = command.visualization_states();
                writeDebugLog("CommandProcessor - Visualization states set");
            }
            writeDebugLog("CommandProcessor - Starting saveRecording() in background thread...");
            std::thread([this]() {
                writeDebugLog("saveRecording thread - Starting...");
                if (recording_manager_) {
                    recording_manager_->saveRecording(camera_manager_->getIntrinsics(),
                                                     *visualization_states_,
                                                     *record_with_yolo_boxes_);
                }
                writeDebugLog("saveRecording thread - Completed");
            }).detach();
            writeDebugLog("CommandProcessor - saveRecording() thread started, sending immediate response");
            response.set_message("Recording started (saving in background)");
            break;
            
        case juggler::v1::CommandRequest::RECORD_CONTINUOUS_START:
            if (record_with_yolo_boxes_) {
                *record_with_yolo_boxes_ = command.record_with_yolo_boxes();
            }
            if (command.has_visualization_states() && visualization_states_) {
                *visualization_states_ = command.visualization_states();
            }
            if (recording_manager_) {
                recording_manager_->startContinuousRecording();
            }
            response.set_message("Continuous recording started");
            break;
            
        case juggler::v1::CommandRequest::RECORD_CONTINUOUS_STOP:
            writeDebugLog("CommandProcessor - RECORD_CONTINUOUS_STOP command received");
            writeDebugLog("CommandProcessor - Starting stopContinuousRecording() in background thread...");
            std::thread([this]() {
                writeDebugLog("stopContinuousRecording thread - Starting...");
                if (recording_manager_ && camera_manager_ && visualization_states_ && record_with_yolo_boxes_) {
                    recording_manager_->stopContinuousRecording(camera_manager_->getIntrinsics(),
                                                               *visualization_states_,
                                                               *record_with_yolo_boxes_);
                }
                writeDebugLog("stopContinuousRecording thread - Completed");
            }).detach();
            writeDebugLog("CommandProcessor - stopContinuousRecording() thread started, sending immediate response");
            response.set_message("Continuous recording stopped (saving in background)");
            break;
            
        case juggler::v1::CommandRequest::CAMERA_STOP:
            if (camera_manager_) {
                camera_manager_->stop();
            }
            response.set_message("Camera feed stopped");
            break;
            
        case juggler::v1::CommandRequest::CAMERA_START:
            if (!command.camera_settings_file().empty() && camera_manager_) {
                if (command.camera_width() > 0 && command.camera_height() > 0 && command.camera_fps() > 0) {
                    camera_manager_->startWithSettings(command.camera_settings_file(),
                                                      command.camera_width(),
                                                      command.camera_height(),
                                                      command.camera_fps());
                    response.set_message("Camera started with settings: " + command.camera_settings_file() +
                                       " at " + std::to_string(command.camera_width()) + "x" + 
                                       std::to_string(command.camera_height()) +
                                       " @ " + std::to_string(command.camera_fps()) + " FPS");
                } else {
                    camera_manager_->startWithSettings(command.camera_settings_file());
                    response.set_message("Camera started with settings: " + command.camera_settings_file());
                }
            } else if (camera_manager_) {
                camera_manager_->start();
                response.set_message("Camera started with current settings");
            }
            break;
            
        case juggler::v1::CommandRequest::CALIBRATE_OBJECT:
            response.set_success(false);
            response.set_message("Manual object calibration not supported in SimpleBallTracker. Use color calibration instead.");
            break;
            
        case juggler::v1::CommandRequest::SET_POSE_MODEL_ENABLED:
            if (tracker_) {
                bool enabled = command.pose_model_enabled();
                bool success = tracker_->updateSetting("enable_pose_detection", enabled ? "1" : "0");
                if (success) {
                    response.set_message(std::string("Pose detection ") + (enabled ? "enabled" : "disabled"));
                } else {
                    response.set_success(false);
                    response.set_message("Failed to update pose detection setting");
                }
            } else {
                response.set_success(false);
                response.set_message("Tracker not initialized");
            }
            break;
            
        case juggler::v1::CommandRequest::CALIBRATE_COLOR:
            if (tracker_) {
                cv::Point click_point(command.click_x(), command.click_y());
                std::string error_message;
                bool success = tracker_->calibrateColor(command.color_name(), click_point, error_message);
                
                if (success) {
                    response.set_message("Color profile '" + command.color_name() + "' calibrated successfully");
                } else {
                    response.set_success(false);
                    response.set_message(error_message);
                }
            } else {
                response.set_success(false);
                response.set_message("Tracker not ready for color calibration");
            }
            break;
            
        case juggler::v1::CommandRequest::ENABLE_FEATURE:
            response.set_message("Feature '" + command.feature_name() + "' enabled (events always sent)");
            break;
            
        case juggler::v1::CommandRequest::DISABLE_FEATURE:
            response.set_message("Feature '" + command.feature_name() + "' disabled (events always sent)");
            break;
            
        case juggler::v1::CommandRequest::SET_VIDEO_FEED_ENABLED:
            if (video_feed_enabled_) {
                *video_feed_enabled_ = command.video_feed_enabled();
            }
            response.set_message(std::string("Video feed encoding ") + 
                               (command.video_feed_enabled() ? "enabled" : "disabled"));
            break;
            
        case juggler::v1::CommandRequest::SET_TRACKER_TYPE:
            try {
                if (tracker_switch_callback_) {
                    tracker_switch_callback_(command.tracker_type());
                }
                response.set_message("Switched to tracker: " + command.tracker_type());
            } catch (const std::exception& e) {
                response.set_success(false);
                response.set_message(std::string("Failed to switch tracker: ") + e.what());
            }
            break;
            
        case juggler::v1::CommandRequest::SET_DEPTH_SENSOR_ENABLED:
            response.set_message(std::string("Depth sensor ") +
                               (command.depth_sensor_enabled() ? "enable" : "disable") +
                               " requested. Note: This requires camera restart to take effect.");
            break;
            
        case juggler::v1::CommandRequest::RELOAD_COLOR_PROFILES:
            if (current_tracker_type_ && *current_tracker_type_ == "new_3d" && new_3d_tracker_) {
                writeDebugLog("CommandProcessor - RELOAD_COLOR_PROFILES command received");
                // Cast to New3DTracker to access reloadColorProfiles()
                auto new_3d = std::dynamic_pointer_cast<New3DTracker>(new_3d_tracker_);
                if (new_3d) {
                    new_3d->reloadColorProfiles();
                    response.set_message("Color profiles reloaded successfully");
                    writeDebugLog("CommandProcessor - Color profiles reloaded");
                } else {
                    response.set_success(false);
                    response.set_message("Failed to cast to New3DTracker");
                }
            } else {
                response.set_success(false);
                std::string current_type = current_tracker_type_ ? *current_tracker_type_ : "unknown";
                response.set_message("Color profile reload only supported for New3D tracker (current: " + current_type + ")");
            }
            break;
            
        case juggler::v1::CommandRequest::PLAYBACK_START:
            writeDebugLog("CommandProcessor - PLAYBACK_START command received");
            if (playback_controller_) {
                // Stop camera first
                if (camera_manager_ && camera_manager_->isRunning()) {
                    writeDebugLog("CommandProcessor - Stopping camera for playback");
                    camera_manager_->stop();
                }
                
                playback_controller_->startPlayback(command.playback_directory());
                if (command.playback_speed() > 0.0f) {
                    playback_controller_->setSpeed(command.playback_speed());
                }
            }
            response.set_message("Playback started");
            break;

        case juggler::v1::CommandRequest::PLAYBACK_STOP:
            writeDebugLog("CommandProcessor - PLAYBACK_STOP command received");
            if (playback_controller_) {
                playback_controller_->stopPlayback();
            }
            response.set_message("Playback stopped");
            break;

        case juggler::v1::CommandRequest::PLAYBACK_STEP_FORWARD:
            writeDebugLog("CommandProcessor - PLAYBACK_STEP_FORWARD command received");
            if (playback_controller_) {
                playback_controller_->stepForward();
            }
            response.set_message("Stepped forward one frame");
            break;

        case juggler::v1::CommandRequest::PLAYBACK_STEP_BACKWARD:
            writeDebugLog("CommandProcessor - PLAYBACK_STEP_BACKWARD command received");
            if (playback_controller_) {
                playback_controller_->stepBackward();
            }
            response.set_message("Stepped backward one frame");
            break;

        case juggler::v1::CommandRequest::PLAYBACK_SET_SPEED:
            writeDebugLog("CommandProcessor - PLAYBACK_SET_SPEED command received");
            if (command.playback_speed() > 0.0f && playback_controller_) {
                playback_controller_->setSpeed(command.playback_speed());
                response.set_message("Playback speed set to " + std::to_string(command.playback_speed()) + "x");
            } else {
                response.set_success(false);
                response.set_message("Missing playback_speed parameter");
            }
            break;

        case juggler::v1::CommandRequest::PLAYBACK_PAUSE:
            writeDebugLog("CommandProcessor - PLAYBACK_PAUSE command received");
            if (playback_controller_) {
                playback_controller_->pause();
            }
            response.set_message("Playback paused");
            break;

        case juggler::v1::CommandRequest::PLAYBACK_RESUME:
            writeDebugLog("CommandProcessor - PLAYBACK_RESUME command received");
            if (playback_controller_) {
                playback_controller_->resume();
            }
            response.set_message("Playback resumed");
            break;

        case juggler::v1::CommandRequest::SET_VISUALIZATION_STATES:
            writeDebugLog("CommandProcessor - SET_VISUALIZATION_STATES command received");
            if (command.has_visualization_states() && visualization_states_) {
                *visualization_states_ = command.visualization_states();
                
                // Synchronize visualization states with tracker's internal settings
                if (tracker_) {
                    const auto& viz = command.visualization_states();
                    
                    // Map UI toggle 'hand_threshold' to tracker's 'show_held_radius'
                    tracker_->updateSetting("show_held_radius",
                                          viz.show_hand_threshold() ? "true" : "false");
                    
                    // Map UI toggle 'color_search' to tracker's 'show_color_search_region'
                    tracker_->updateSetting("show_color_search_region",
                                          viz.show_color_search() ? "true" : "false");
                    
                    writeDebugLog("CommandProcessor - Synchronized visualization toggles with tracker settings");
                }
                
                response.set_message("Visualization states updated");
                writeDebugLog("CommandProcessor - Visualization states updated successfully");
            } else {
                response.set_success(false);
                response.set_message("Missing visualization_states parameter");
            }
            break;

        default:
            response.set_success(false);
            response.set_message("Unknown command");
            break;
    }
}

void CommandProcessor::handleInternalCommand(const juggler::v1::CommandRequest& command) {
    switch (command.type()) {
        case juggler::v1::CommandRequest::SEND_COLOR_COMMAND:
            if (color_module_) {
                color_module_->processCommand(command);
            }
            break;
        default:
            // Other internal commands can be handled here
            break;
    }
}

std::unique_ptr<ModuleBase> CommandProcessor::createModule(const juggler::v1::CommandRequest& command) {
    const std::string& module_name = command.module_name();
    if (module_name == "UdpBallColorModule") {
        return std::make_unique<UdpBallColorModule>();
    }
    if (module_name == "PositionToRgbModule") {
        return std::make_unique<PositionToRgbModule>();
    }
    return nullptr;
}