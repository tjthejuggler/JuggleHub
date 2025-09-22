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
    last_update_time_ = std::chrono::steady_clock::now();
}

DNNTracker::~DNNTracker() {}

std::pair<std::vector<TrackedObject>, std::vector<RawDetection>> DNNTracker::update(const cv::Mat& color_frame, const cv::Mat& depth_frame, const CameraIntrinsics& intrinsics) {
    auto current_time = std::chrono::steady_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::duration<float>>(current_time - last_update_time_).count();
    last_update_time_ = current_time;

    // --- 1. PREDICTION ---
    for (auto& pair : kalman_filters_) {
        int track_id = pair.first;
        if (track_class_ids_.count(track_id)) {
            int class_id = track_class_ids_[track_id];
            if (class_id == 0 || class_id == 1 || class_id == 2) { // Ball
                pair.second.predict_ball(dt);
            } else { // Hand
                pair.second.predict(dt);
            }
        } else {
            pair.second.predict(dt);
        }
    }

    // --- 2. DETECTION ---
    float scale_x, scale_y;
    cv::Mat preprocessed_image = preprocess(color_frame, scale_x, scale_y);
    ov::Tensor input_tensor(compiled_model.input().get_element_type(), compiled_model.input().get_shape(), preprocessed_image.data);
    infer_request.set_input_tensor(input_tensor);
    infer_request.infer();
    const ov::Tensor& output_tensor = infer_request.get_output_tensor();
    std::vector<RawDetection> raw_detections;
    std::vector<byte_track::Object> detections_for_bytetrack = postprocess(color_frame, depth_frame, intrinsics, output_tensor, scale_x, scale_y, raw_detections);

    // --- 3. TRACKING (via ByteTrack) ---
    std::vector<std::shared_ptr<byte_track::STrack>> tracks = tracker->update(detections_for_bytetrack);

    std::vector<TrackedObject> tracked_objects;
    std::set<int> active_track_ids;
    std::set<int> associated_detection_indices;

    // --- 4. ASSOCIATION & UPDATE ---
    for (const auto& track : tracks) {
        int track_id = (int)track->getTrackId();
        active_track_ids.insert(track_id);
        const auto& track_rect = track->getRect();
        int best_detection_idx = -1;

        if (kalman_filters_.count(track_id)) {
            // --- Logic for EXISTING tracks: Associate via 3D distance ---
            float min_dist = 0.3f; // 30cm search radius
            cv::Point3f predicted_pos;
            auto state = kalman_filters_.at(track_id).get_position();
            predicted_pos = cv::Point3f(state.x(), state.y(), state.z());

            for (int i = 0; i < raw_detections.size(); ++i) {
                if (associated_detection_indices.count(i)) continue;
                if (raw_detections[i].world_pos.z > 0) {
                    float dist = calculate_distance(predicted_pos, raw_detections[i].world_pos);
                    if (dist < min_dist) {
                        min_dist = dist;
                        best_detection_idx = i;
                    }
                }
            }
            if (best_detection_idx != -1) {
                const auto& matched_det = raw_detections[best_detection_idx];
                kalman_filters_[track_id].update(KalmanFilter3D::MeasurementVector(matched_det.world_pos.x, matched_det.world_pos.y, matched_det.world_pos.z));
                track_class_ids_[track_id] = matched_det.class_id;
            }
        } else {
            // --- Logic for NEW tracks: Associate via 2D IoU to initialize ---
            float best_iou = 0.5f;
            for (int i = 0; i < raw_detections.size(); ++i) {
                if (associated_detection_indices.count(i)) continue;
                const auto& det_box = raw_detections[i].box;
                byte_track::Rect<float> det_rect(det_box.x, det_box.y, det_box.width, det_box.height);
                float iou = calculate_iou(track_rect, det_rect);
                if (iou > best_iou) {
                    best_iou = iou;
                    best_detection_idx = i;
                }
            }
            if (best_detection_idx != -1) {
                const auto& matched_det = raw_detections[best_detection_idx];
                kalman_filters_[track_id].init(KalmanFilter3D::MeasurementVector(matched_det.world_pos.x, matched_det.world_pos.y, matched_det.world_pos.z));
                track_class_ids_[track_id] = matched_det.class_id;
            }
        }

        if (best_detection_idx != -1) {
            associated_detection_indices.insert(best_detection_idx);
        }

        // Create the TrackedObject with the smoothed, filtered position
        auto filtered_state = kalman_filters_[track_id].get_position();
        cv::Point3f filtered_pos(filtered_state.x(), filtered_state.y(), filtered_state.z());
        
        // If this ball is held, overwrite its position with the hand's position.
        if (held_ball_states_.count(track_id)) {
            int hand_id = held_ball_states_[track_id];
            if (kalman_filters_.count(hand_id)) {
                auto hand_state = kalman_filters_[hand_id].get_position();
                filtered_pos.x = hand_state.x();
                filtered_pos.y = hand_state.y();
                filtered_pos.z = hand_state.z();
            }
        }

        int class_id = track_class_ids_.count(track_id) ? track_class_ids_[track_id] : -1;
        std::string class_name = (class_id != -1 && class_id < class_names_.size()) ? class_names_[class_id] : "unknown";

        tracked_objects.push_back({
            cv::Rect_<float>(track->getRect().x(), track->getRect().y(), track->getRect().width(), track->getRect().height()),
            filtered_pos,
            track_id,
            class_id,
            class_name
        });
    }
    
    // --- 5. CLEANUP ---
    std::vector<int> tracks_to_remove;
    for (const auto& pair : kalman_filters_) {
        if (active_track_ids.find(pair.first) == active_track_ids.end()) {
            tracks_to_remove.push_back(pair.first);
        }
    }
    for (int id : tracks_to_remove) {
        kalman_filters_.erase(id);
        track_class_ids_.erase(id);
    }

    manage_hand_tracks(tracked_objects, raw_detections);
    manage_ball_occlusion(tracked_objects);

    return {tracked_objects, raw_detections};
}

void DNNTracker::manage_hand_tracks(std::vector<TrackedObject>& tracks, const std::vector<RawDetection>& raw_detections) {
    // 1. Filter all raw detections to find only hands.
    std::vector<RawDetection> hand_detections;
    for (const auto& det : raw_detections) {
        if (det.class_id == 3) { // Assuming class_id 3 is "hand"
            hand_detections.push_back(det);
        }
    }

    // 2. Sort hand detections by confidence.
    std::sort(hand_detections.begin(), hand_detections.end(), [](const RawDetection& a, const RawDetection& b) {
        return a.confidence > b.confidence;
    });

    // 3. Identify the top two hand candidates.
    std::vector<RawDetection> top_hands;
    if (hand_detections.size() > 0) top_hands.push_back(hand_detections[0]);
    if (hand_detections.size() > 1) top_hands.push_back(hand_detections[1]);

    // 4. Identify the final track IDs for left and right hands.
    int current_left_id = -1;
    int current_right_id = -1;

    if (top_hands.size() == 1) {
        // If one hand, find its track and decide if it's left or right based on position.
        // For now, let's just assign it to the closest existing hand track or make a new one.
        // This logic will be more robust later.
        // Find the track associated with this detection.
        for(const auto& track : tracks) {
            if (track.box.contains(cv::Point2f(top_hands[0].box.x + top_hands[0].box.width / 2, top_hands[0].box.y + top_hands[0].box.height / 2))) {
                if (top_hands[0].box.x < 320) { // rough center screen split
                    current_left_id = track.id;
                } else {
                    current_right_id = track.id;
                }
                break;
            }
        }
    } else if (top_hands.size() == 2) {
        // Determine which is left and which is right.
        RawDetection& hand1 = top_hands[0];
        RawDetection& hand2 = top_hands[1];
        RawDetection* left_hand_det = (hand1.box.x < hand2.box.x) ? &hand1 : &hand2;
        RawDetection* right_hand_det = (hand1.box.x < hand2.box.x) ? &hand2 : &hand1;

        for(const auto& track : tracks) {
            if (track.box.contains(cv::Point2f(left_hand_det->box.x + left_hand_det->box.width / 2, left_hand_det->box.y + left_hand_det->box.height / 2))) {
                current_left_id = track.id;
            }
            if (track.box.contains(cv::Point2f(right_hand_det->box.x + right_hand_det->box.width / 2, right_hand_det->box.y + right_hand_det->box.height / 2))) {
                current_right_id = track.id;
            }
        }
    }
    
    left_hand_track_id_ = current_left_id;
    right_hand_track_id_ = current_right_id;

    // 5. Cull any other tracks that are incorrectly classified as hands.
    std::vector<TrackedObject> final_tracks;
    for (const auto& track : tracks) {
        if (track.class_id == 3) { // It's a hand
            if (track.id == left_hand_track_id_ || track.id == right_hand_track_id_) {
                final_tracks.push_back(track);
            }
        } else { // Not a hand, keep it
            final_tracks.push_back(track);
        }
    }
    tracks = final_tracks;
}

void DNNTracker::manage_ball_occlusion(std::vector<TrackedObject>& tracks) {
    // Logic to determine if a ball is "in hand"
    const float occlusion_threshold = 0.1f; // 10cm distance threshold
    std::set<int> balls_to_remove_from_occlusion;

    // First, check if any currently held balls should be released.
    for (auto const& [ball_id, hand_id] : held_ball_states_) {
        bool ball_is_visible = false;
        for (const auto& track : tracks) {
            if (track.id == ball_id) {
                ball_is_visible = true;
                break;
            }
        }
        if (ball_is_visible) {
            balls_to_remove_from_occlusion.insert(ball_id);
        }
    }

    for (int ball_id : balls_to_remove_from_occlusion) {
        held_ball_states_.erase(ball_id);
    }

    // Next, check for new occlusions.
    for (const auto& track : tracks) {
        if (track.class_id >= 0 && track.class_id <= 2) { // It's a ball
            if (held_ball_states_.count(track.id)) continue; // Already handled

            cv::Point3f ball_pos = track.world_pos;
            
            // Check against left hand
            if (left_hand_track_id_ != -1 && kalman_filters_.count(left_hand_track_id_)) {
                auto hand_state = kalman_filters_[left_hand_track_id_].get_position();
                cv::Point3f hand_pos(hand_state.x(), hand_state.y(), hand_state.z());
                if (calculate_distance(ball_pos, hand_pos) < occlusion_threshold) {
                    held_ball_states_[track.id] = left_hand_track_id_;
                }
            }
            
            // Check against right hand
            if (right_hand_track_id_ != -1 && kalman_filters_.count(right_hand_track_id_)) {
                auto hand_state = kalman_filters_[right_hand_track_id_].get_position();
                cv::Point3f hand_pos(hand_state.x(), hand_state.y(), hand_state.z());
                if (calculate_distance(ball_pos, hand_pos) < occlusion_threshold) {
                    held_ball_states_[track.id] = right_hand_track_id_;
                }
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