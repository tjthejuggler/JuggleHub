#include "DNNTracker.hpp"
#include <iostream>
#include <algorithm> // Required for std::max and std::min
#include <cmath>     // Required for std::sqrt
#include <set>

// --- HELPER FUNCTIONS ---
static cv::Point3f deproject_2d_to_3d(const cv::Point2f& pixel, float depth, const CameraIntrinsics& intrinsics) {
    if (depth > 0) {
        float x = (pixel.x - intrinsics.ppx) * depth / intrinsics.fx;
        float y = (pixel.y - intrinsics.ppy) * depth / intrinsics.fy;
        return cv::Point3f(x, y, depth);
    }
    return cv::Point3f(0.0f, 0.0f, 0.0f);
}

cv::Point2f DNNTracker::project_3d_to_2d(const cv::Point3f& world_pos, const CameraIntrinsics& intrinsics) {
    if (world_pos.z > 0) {
        float x_2d = (world_pos.x * intrinsics.fx) / world_pos.z + intrinsics.ppx;
        float y_2d = (world_pos.y * intrinsics.fy) / world_pos.z + intrinsics.ppy;
        return cv::Point2f(x_2d, y_2d);
    }
    return cv::Point2f(-1, -1); // Invalid point
}

static float calculate_distance(const cv::Point3f& p1, const cv::Point3f& p2) {
    return std::sqrt(std::pow(p1.x - p2.x, 2) +
                     std::pow(p1.y - p2.y, 2) +
                     std::pow(p1.z - p2.z, 2));
}

static float calculate_iou(const byte_track::Rect<float>& box1, const byte_track::Rect<float>& box2) {
    float xA = std::max(box1.x(), box2.x());
    float yA = std::max(box1.y(), box2.y());
    float xB = std::min(box1.x() + box1.width(), box2.x() + box2.width());
    float yB = std::min(box1.y() + box1.height(), box2.y() + box1.height());
    float intersection_area = std::max(0.0f, xB - xA) * std::max(0.0f, yB - yA);
    float box1_area = box1.width() * box1.height();
    float box2_area = box2.width() * box2.height();
    float union_area = box1_area + box2_area - intersection_area;
    return (union_area > 0) ? intersection_area / union_area : 0.0f;
}

DNNTracker::DNNTracker(const std::string& model_path, const std::string& device_name) {
    std::cout << "Loading OpenVINO model: " << model_path << std::endl;
    std::cout << "Compiling model for device: " << device_name << std::endl;
    compiled_model = core.compile_model(model_path, device_name);
    infer_request = compiled_model.create_infer_request();
    std::cout << "Model loaded successfully." << std::endl;
    reinitialize_tracker();
    initialize_logical_trackers();
    last_update_time_ = std::chrono::steady_clock::now();
}

DNNTracker::~DNNTracker() {}

void DNNTracker::initialize_logical_trackers() {
    logical_ball_trackers_.clear();
    for (int i = 0; i < NUM_BALLS; ++i) {
        logical_ball_trackers_.emplace_back(i, "ball");
    }

    logical_hand_trackers_.clear();
    for (int i = 0; i < NUM_HANDS; ++i) {
        logical_hand_trackers_.emplace_back(i + NUM_BALLS, "hand"); // Give hands unique IDs
    }
}

std::pair<std::vector<TrackedObject>, std::vector<RawDetection>> DNNTracker::update(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics) {
    auto current_time = std::chrono::steady_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::duration<float>>(current_time - last_update_time_).count();
    last_update_time_ = current_time;

    // --- 1. PREDICT ---
    for (auto& ball : logical_ball_trackers_) {
        if (ball.status != TrackerStatus::LOST) {
            if (ball.is_in_freefall) {
                ball.kf.predict_ball(dt);
            } else {
                ball.kf.predict(dt); // Constant velocity prediction if held or stationary
            }
        }
    }
    for (auto& hand : logical_hand_trackers_) {
        if (hand.status != TrackerStatus::LOST) hand.kf.predict(dt);
    }

    // --- 2. DETECT ---
    float scale_x, scale_y;
    cv::Mat preprocessed_image = preprocess(color_frame, scale_x, scale_y);
    ov::Tensor input_tensor(compiled_model.input().get_element_type(), compiled_model.input().get_shape(), preprocessed_image.data);
    infer_request.set_input_tensor(input_tensor);
    infer_request.infer();
    const ov::Tensor& output_tensor = infer_request.get_output_tensor();
    last_raw_detections_.clear();
    std::vector<byte_track::Object> detections_for_bytetrack = postprocess(color_frame, depth_frame, intrinsics, output_tensor, scale_x, scale_y, last_raw_detections_);

    // --- 3. TRACK (ByteTrack) ---
    std::vector<std::shared_ptr<byte_track::STrack>> byte_tracks = tracker->update(detections_for_bytetrack);

    // --- 4. ASSOCIATE Persistent Trackers with ByteTrack Tracks ---
    std::set<int> matched_byte_track_ids;
    
    std::vector<PersistentTracker*> all_logical_trackers;
    for(auto& ball : logical_ball_trackers_) all_logical_trackers.push_back(&ball);
    for(auto& hand : logical_hand_trackers_) all_logical_trackers.push_back(&hand);

    for(auto* tracker : all_logical_trackers) {
        if (tracker->status == TrackerStatus::TRACKED) tracker->status = TrackerStatus::PREDICTED;
        if (tracker->status != TrackerStatus::LOST) tracker->frames_since_seen++;
    }

    for (const auto& b_track : byte_tracks) {
        int b_track_id = (int)b_track->getTrackId();
        
        const RawDetection* best_det = nullptr;
        float best_iou = 0.0f;
        for(const auto& det : last_raw_detections_){
            byte_track::Rect<float> det_rect(det.box.x, det.box.y, det.box.width, det.box.height);
            float iou = calculate_iou(b_track->getRect(), det_rect);
            if(iou > best_iou){
                best_iou = iou;
                best_det = &det;
            }
        }
        if (!best_det || best_det->world_pos.z < 0.2f || best_det->world_pos.z > 2.0f) continue;

        PersistentTracker* best_match_tracker = nullptr;

        for(auto* p_tracker : all_logical_trackers) {
            if (p_tracker->last_seen_bytetrack_id == b_track_id && p_tracker->class_name == (best_det->class_id == 3 ? "hand" : "ball")) {
                best_match_tracker = p_tracker;
                break;
            }
        }
        
        if (!best_match_tracker) {
            float min_dist = 0.5f; // Increased to 50 cm for more robust re-acquisition
            for(auto* p_tracker : all_logical_trackers) {
                if ((p_tracker->status == TrackerStatus::PREDICTED || p_tracker->status == TrackerStatus::OCCLUDED) && p_tracker->class_name == (best_det->class_id == 3 ? "hand" : "ball")) {
                    p_tracker->update_from_kf();
                    cv::Point3f predicted_pos(p_tracker->position.x(), p_tracker->position.y(), p_tracker->position.z());
                    float dist = calculate_distance(predicted_pos, best_det->world_pos);
                    if (dist < min_dist) {
                        min_dist = dist;
                        best_match_tracker = p_tracker;
                    }
                }
            }
            if(best_match_tracker) {
                std::cout << "Re-acquired predicted tracker " << best_match_tracker->logical_id << " with new bytetrack_id " << b_track_id << std::endl;
            }
        }
        
        if (!best_match_tracker) {
            for(auto* p_tracker : all_logical_trackers) {
                 if (p_tracker->status == TrackerStatus::LOST && p_tracker->class_name == (best_det->class_id == 3 ? "hand" : "ball")) {
                     best_match_tracker = p_tracker;
                     best_match_tracker->kf.init(KalmanFilter3D::MeasurementVector(best_det->world_pos.x, best_det->world_pos.y, best_det->world_pos.z));
                     break;
                 }
            }
        }

        if (best_match_tracker) {
            best_match_tracker->kf.update(KalmanFilter3D::MeasurementVector(best_det->world_pos.x, best_det->world_pos.y, best_det->world_pos.z));
            best_match_tracker->status = TrackerStatus::TRACKED;
            best_match_tracker->box_2d = best_det->box;
            best_match_tracker->frames_since_seen = 0;
            best_match_tracker->last_seen_bytetrack_id = b_track_id;
            best_match_tracker->parent_id = -1;
            matched_byte_track_ids.insert(b_track_id);
        }
    }

    // --- 5. MANAGE HEURISTICS ---
    std::vector<RawDetection> hand_detections;
    for(const auto& det : last_raw_detections_) if(det.class_id == 3) hand_detections.push_back(det);
    manage_hand_tracks(hand_detections);
    manage_ball_occlusion();

    // --- 6. COMPILE FINAL RESULTS ---
    std::vector<TrackedObject> final_tracked_objects;
    for(auto* tracker : all_logical_trackers) {
        if (tracker->status == TrackerStatus::LOST) continue;
        
        tracker->update_from_kf();
        auto pos = tracker->position;

        final_tracked_objects.push_back({
            tracker->box_2d,
            cv::Point3f(pos.x(), pos.y(), pos.z()),
            tracker->last_seen_bytetrack_id,
            -1, // class id
            tracker->class_name,
            tracker->status,
            tracker->logical_id,
            tracker->is_left_hand
        });
    }

    return {final_tracked_objects, last_raw_detections_};
}


void DNNTracker::manage_hand_tracks(const std::vector<RawDetection>& hand_detections) {
    // This logic is now stateful. It tries to maintain left/right assignment.
    PersistentTracker* left_hand = nullptr;
    PersistentTracker* right_hand = nullptr;
    for(auto& hand : logical_hand_trackers_) {
        if (hand.is_left_hand) left_hand = &hand;
        else right_hand = &hand;
    }

    // Simple assignment based on x-coordinate for now if both are visible
    if (logical_hand_trackers_[0].status == TrackerStatus::TRACKED && logical_hand_trackers_[1].status == TrackerStatus::TRACKED) {
         if (logical_hand_trackers_[0].position.x() < logical_hand_trackers_[1].position.x()) {
            logical_hand_trackers_[0].is_left_hand = true;
            logical_hand_trackers_[1].is_left_hand = false;
         } else {
            logical_hand_trackers_[0].is_left_hand = false;
            logical_hand_trackers_[1].is_left_hand = true;
         }
    }
}


void DNNTracker::manage_ball_occlusion() {
    const float CATCH_THRESHOLD = 0.15f; // 15cm distance threshold for a catch
    const float THROW_THRESHOLD = 0.20f; // 20cm distance threshold for a throw

    PersistentTracker* left_hand = nullptr;
    PersistentTracker* right_hand = nullptr;
    for(auto& hand : logical_hand_trackers_) {
        if (hand.is_left_hand) left_hand = &hand;
        else right_hand = &hand;
    }

    for (auto& ball : logical_ball_trackers_) {
        // --- CATCH LOGIC ---
        // A predicted (unseen) ball is considered caught if it gets close to a tracked hand.
        if (ball.status == TrackerStatus::PREDICTED) {
            ball.update_from_kf();
            cv::Point3f predicted_ball_pos(ball.position.x(), ball.position.y(), ball.position.z());
            
            auto check_catch = [&](PersistentTracker* hand) {
                if (hand && hand->status == TrackerStatus::TRACKED) {
                    hand->update_from_kf();
                    cv::Point3f hand_pos(hand->position.x(), hand->position.y(), hand->position.z());
                    if (calculate_distance(predicted_ball_pos, hand_pos) < CATCH_THRESHOLD) {
                        ball.status = TrackerStatus::OCCLUDED;
                        ball.parent_id = hand->logical_id;
                        ball.is_in_freefall = false; // The ball has been caught, stop gravity.
                        // Snap ball position and velocity to the hand's state
                        ball.position = hand->position;
                        KalmanFilter3D::StateVector hand_state = hand->kf.get_state();
                        KalmanFilter3D::StateVector& ball_state = ball.kf.get_state();
                        ball_state.tail<3>() = hand_state.tail<3>();
                        return true;
                    }
                }
                return false;
            };

            if (check_catch(left_hand)) continue;
            check_catch(right_hand);
        }

        // --- THROW LOGIC ---
        // An occluded ball is considered thrown if it becomes tracked again far from its parent hand.
        if (ball.status == TrackerStatus::TRACKED && ball.parent_id != -1) {
             PersistentTracker* parent_hand = nullptr;
             for(auto& hand : logical_hand_trackers_) {
                 if(hand.logical_id == ball.parent_id) {
                     parent_hand = &hand;
                     break;
                 }
             }

            if (parent_hand && parent_hand->status == TrackerStatus::TRACKED) {
                ball.update_from_kf();
                parent_hand->update_from_kf();
                cv::Point3f ball_pos(ball.position.x(), ball.position.y(), ball.position.z());
                cv::Point3f hand_pos(parent_hand->position.x(), parent_hand->position.y(), parent_hand->position.z());

                if (calculate_distance(ball_pos, hand_pos) > THROW_THRESHOLD) {
                    ball.is_in_freefall = true; // The ball has been thrown, start gravity.
                    ball.parent_id = -1; // It is no longer associated with the hand.
                }
            } else {
                // If the parent hand is lost, the ball is also considered thrown.
                ball.is_in_freefall = true;
                ball.parent_id = -1;
            }
        }
    }
}

void DNNTracker::calibrate_object(int logical_id, const cv::Point2f& pixel_coords, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics) {
    float min_dist = 20.0f; // 20 pixel search radius
    const RawDetection* closest_det = nullptr;

    for(const auto& det : last_raw_detections_) {
        cv::Point2f center(det.box.x + det.box.width / 2, det.box.y + det.box.height / 2);
        float dist = cv::norm(pixel_coords - center);
        if (dist < min_dist) {
            min_dist = dist;
            closest_det = &det;
        }
    }

    if (closest_det) {
        // Find the logical tracker
        for (auto& tracker : logical_ball_trackers_) {
            if (tracker.logical_id == logical_id) {
                if (closest_det->world_pos.z > 0.2 && closest_det->world_pos.z < 2.0) {
                     tracker.kf.init(KalmanFilter3D::MeasurementVector(closest_det->world_pos.x, closest_det->world_pos.y, closest_det->world_pos.z));
                     tracker.status = TrackerStatus::TRACKED;
                     tracker.frames_since_seen = 0;
                     tracker.is_in_freefall = false; // When calibrating, assume it's held/stationary.
                     std::cout << "Calibrated Ball " << logical_id << " at " << closest_det->world_pos << std::endl;
                }
                return;
            }
        }
    }
}

void DNNTracker::update_setting(const std::string& key, const std::string& value) {
    try {
        if (key == "confidence_threshold") confidence_threshold_ = std::stof(value);
        else if (key == "nms_threshold") nms_threshold_ = std::stof(value);
        else if (key == "track_buffer") { track_buffer_ = std::stoi(value); reinitialize_tracker(); }
        else if (key == "track_thresh") { track_thresh_ = std::stof(value); reinitialize_tracker(); }
        else if (key == "high_thresh") { high_thresh_ = std::stof(value); reinitialize_tracker(); }
        else if (key == "match_thresh") { match_thresh_ = std::stof(value); reinitialize_tracker(); }
        else std::cerr << "Warning: Unknown DNNTracker setting key '" << key << "'" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error updating setting " << key << " with value " << value << ": " << e.what() << std::endl;
    }
}

void DNNTracker::reinitialize_tracker() {
    int frame_rate = 30;
    tracker = std::make_unique<byte_track::BYTETracker>(frame_rate, track_buffer_, track_thresh_, high_thresh_, match_thresh_);
}

cv::Mat DNNTracker::preprocess(const cv::Mat& frame, float& scale_x, float& scale_y) {
    cv::Mat resized_frame;
    cv::resize(frame, resized_frame, cv::Size(input_width_, input_height_));
    scale_x = (float)frame.cols / input_width_;
    scale_y = (float)frame.rows / input_height_;
    cv::Mat float_frame;
    resized_frame.convertTo(float_frame, CV_32F, 1.0 / 255.0);
    return cv::dnn::blobFromImage(float_frame);
}

std::vector<byte_track::Object> DNNTracker::postprocess(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics, const ov::Tensor& output_tensor, float scale_x, float scale_y, std::vector<RawDetection>& raw_detections) {
    raw_detections.clear();
    const float* output_data = output_tensor.data<const float>();
    const int num_channels = 4 + num_classes_;
    
    cv::Mat output_buffer(num_channels, output_tensor.get_shape()[2], CV_32F, (void*)output_data);
    cv::transpose(output_buffer, output_buffer);

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;
    
    float raw_detection_threshold = 0.1f;

    for (int i = 0; i < output_buffer.rows; ++i) {
        cv::Mat class_scores = output_buffer.row(i).colRange(4, num_channels);
        cv::Point class_id_point;
        double max_class_score;
        cv::minMaxLoc(class_scores, nullptr, &max_class_score, nullptr, &class_id_point);

        float confidence = static_cast<float>(max_class_score);
        
        float cx = output_buffer.at<float>(i, 0);
        float cy = output_buffer.at<float>(i, 1);
        float w = output_buffer.at<float>(i, 2);
        float h = output_buffer.at<float>(i, 3);
        int left = static_cast<int>((cx - 0.5 * w) * scale_x);
        int top = static_cast<int>((cy - 0.5 * h) * scale_y);
        int width = static_cast<int>(w * scale_x);
        int height = static_cast<int>(h * scale_y);

        if (confidence > confidence_threshold_) {
            boxes.push_back(cv::Rect(left, top, width, height));
            confidences.push_back(confidence);
            class_ids.push_back(class_id_point.x);
        }

        if (confidence > raw_detection_threshold) {
            cv::Point2f center_pixel(left + width / 2.0f, top + height / 2.0f);
            cv::Point3f world_pos(0,0,0);

            if (center_pixel.x >= 0 && center_pixel.x < depth_frame.cols &&
                center_pixel.y >= 0 && center_pixel.y < depth_frame.rows) {
                uint16_t depth_value_mm = depth_frame.at<uint16_t>(center_pixel.y, center_pixel.x);
                float depth_value_m = depth_value_mm / 1000.0f;
                world_pos = deproject_2d_to_3d(center_pixel, depth_value_m, intrinsics);
            }
            raw_detections.push_back({cv::Rect_<float>(left, top, width, height), world_pos, confidence, class_id_point.x});
        }
    }

    std::vector<int> nms_indices;
    cv::dnn::NMSBoxes(boxes, confidences, confidence_threshold_, nms_threshold_, nms_indices);

    std::vector<byte_track::Object> objects;
    for (int index : nms_indices) {
        objects.push_back({
            byte_track::Rect<float>(boxes[index].x, boxes[index].y, boxes[index].width, boxes[index].height),
            class_ids[index],
            confidences[index]
        });
    }
    
    return objects;
}