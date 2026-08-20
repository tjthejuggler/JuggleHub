#include "ColorOnlyTracker.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================================
// CONSTRUCTION / SETTINGS
// ============================================================================

ColorOnlyTracker::ColorOnlyTracker(const std::string& settings_file)
    : settings_file_(settings_file) {
    std::cout << "[ColorOnlyTracker] Initializing (identity-free color-class tracking)..." << std::endl;

    loadSettings();
    loadColorProfiles();

    int calibrated = 0;
    for (const auto& p : color_profiles_) {
        if (p.enabled && p.avg_hue >= 0.0f && p.avg_saturation >= 0.0f) calibrated++;
    }
    std::cout << "[ColorOnlyTracker] Ready. " << color_profiles_.size() << " profiles loaded, "
              << calibrated << " enabled+calibrated." << std::endl;
    std::cout << "[ColorOnlyTracker] Color profiles source: " << color_profiles_file_ << std::endl;
}

bool ColorOnlyTracker::loadSettings() {
    try {
        if (!fs::exists(settings_file_)) {
            std::cout << "[ColorOnlyTracker] No settings file yet (" << settings_file_
                      << "), using defaults." << std::endl;
            return false;
        }

        std::ifstream file(settings_file_);
        if (!file.is_open()) return false;

        json j;
        file >> j;

        if (j.contains("use_depth_filter"))    settings_.use_depth_filter = j["use_depth_filter"].get<bool>();
        if (j.contains("min_distance_cm"))     settings_.min_distance_m = j["min_distance_cm"].get<float>() / 100.0f;
        if (j.contains("max_distance_cm"))     settings_.max_distance_m = j["max_distance_cm"].get<float>() / 100.0f;
        if (j.contains("hue_tolerance"))       settings_.hue_tolerance = j["hue_tolerance"].get<int>();
        if (j.contains("sat_minimum"))         settings_.sat_minimum = j["sat_minimum"].get<int>();
        if (j.contains("val_minimum"))         settings_.val_minimum = j["val_minimum"].get<int>();
        if (j.contains("min_area_cm2"))        settings_.min_area_cm2 = j["min_area_cm2"].get<float>();
        if (j.contains("max_area_cm2"))        settings_.max_area_cm2 = j["max_area_cm2"].get<float>();
        if (j.contains("min_circularity"))     settings_.min_circularity = j["min_circularity"].get<float>();
        if (j.contains("max_balls_per_color")) settings_.max_balls_per_color = j["max_balls_per_color"].get<int>();
        if (j.contains("debug_logging"))       settings_.debug_logging = j["debug_logging"].get<bool>();

        std::cout << "[ColorOnlyTracker] Settings loaded from " << settings_file_ << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ColorOnlyTracker] Error loading settings: " << e.what() << std::endl;
        return false;
    }
}

void ColorOnlyTracker::saveSettings() const {
    try {
        json j;
        j["tracker_type"] = "color_only";
        j["use_depth_filter"] = settings_.use_depth_filter;
        j["min_distance_cm"] = settings_.min_distance_m * 100.0f;
        j["max_distance_cm"] = settings_.max_distance_m * 100.0f;
        j["hue_tolerance"] = settings_.hue_tolerance;
        j["sat_minimum"] = settings_.sat_minimum;
        j["val_minimum"] = settings_.val_minimum;
        j["min_area_cm2"] = settings_.min_area_cm2;
        j["max_area_cm2"] = settings_.max_area_cm2;
        j["min_circularity"] = settings_.min_circularity;
        j["max_balls_per_color"] = settings_.max_balls_per_color;
        j["debug_logging"] = settings_.debug_logging;

        std::ofstream file(settings_file_);
        file << j.dump(4);
    } catch (const std::exception& e) {
        std::cerr << "[ColorOnlyTracker] Error saving settings: " << e.what() << std::endl;
    }
}

// ============================================================================
// COLOR PROFILES (shared with New3DTracker / hub calibration UI)
// ============================================================================

void ColorOnlyTracker::loadColorProfiles() {
    color_profiles_.clear();

    try {
        if (!fs::exists(color_profiles_file_)) {
            std::cerr << "[ColorOnlyTracker] Color profile file not found: "
                      << color_profiles_file_ << std::endl;
            return;
        }

        std::ifstream file(color_profiles_file_);
        if (!file.is_open()) return;

        json j;
        file >> j;

        if (!j.contains("color_profiles")) {
            std::cout << "[ColorOnlyTracker] No 'color_profiles' key in "
                      << color_profiles_file_ << std::endl;
            return;
        }

        for (const auto& pj : j["color_profiles"]) {
            ColorProfile profile;
            profile.name = pj.value("name", std::string(""));
            profile.enabled = pj.value("enabled", true);
            profile.avg_hue = pj.value("avg_hue", -1.0f);
            profile.avg_saturation = pj.value("avg_saturation", -1.0f);

            auto read_scalar3 = [](const json& node, const char* key, cv::Scalar& out, float def_val) {
                if (node.contains(key) && node[key].is_array() && node[key].size() >= 3) {
                    out = cv::Scalar(node[key][0].get<float>(),
                                     node[key][1].get<float>(),
                                     node[key][2].get<float>());
                } else {
                    out = cv::Scalar(def_val, def_val, def_val);
                }
            };

            read_scalar3(pj, "min_hsv", profile.min_hsv, 0.0f);
            read_scalar3(pj, "max_hsv", profile.max_hsv, 180.0f);
            read_scalar3(pj, "min_hsv2", profile.min_hsv2, -1.0f);
            read_scalar3(pj, "max_hsv2", profile.max_hsv2, -1.0f);

            if (!profile.name.empty()) {
                color_profiles_.push_back(profile);
            }
        }

        std::cout << "[ColorOnlyTracker] Loaded " << color_profiles_.size()
                  << " color profiles from " << color_profiles_file_ << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ColorOnlyTracker] Error loading color profiles: " << e.what() << std::endl;
    }
}

void ColorOnlyTracker::reloadColorProfiles() {
    std::cout << "[ColorOnlyTracker] Reloading color profiles..." << std::endl;
    loadColorProfiles();

    int calibrated = 0;
    for (const auto& p : color_profiles_) {
        if (p.enabled && p.avg_hue >= 0.0f && p.avg_saturation >= 0.0f) calibrated++;
    }
    std::cout << "[ColorOnlyTracker] Reloaded. Tracking " << calibrated
              << " enabled+calibrated colors." << std::endl;
}

// ============================================================================
// CORE DETECTION (per frame, identity-free)
// ============================================================================

cv::Point3f ColorOnlyTracker::deprojectToWorld(const cv::Point2f& pixel, float depth_m,
                                               const CameraIntrinsics& intr) const {
    return cv::Point3f(
        (pixel.x - intr.ppx) * depth_m / intr.fx,
        (pixel.y - intr.ppy) * depth_m / intr.fy,
        depth_m
    );
}

std::pair<std::vector<SimpleBall>, std::vector<BallEvent>>
ColorOnlyTracker::update(const cv::Mat& color_image, const cv::Mat& depth_image,
                         const CameraIntrinsics& intrinsics) {
    std::vector<SimpleBall> balls;
    std::vector<BallEvent> events;  // Always empty: no throw/catch detection by design

    if (color_image.empty()) {
        last_balls_ = balls;
        last_detections_.clear();
        return {balls, events};
    }

    // Cache frame for click-based calibration
    last_color_image_ = color_image.clone();

    const bool depth_available = !depth_image.empty() &&
                                 depth_image.type() == CV_16UC1 &&
                                 settings_.use_depth_filter;

    // ---- Step 1: depth-range mask (background removal) ----
    cv::Mat depth_mask;
    if (depth_available) {
        depth_mask = cv::Mat::zeros(depth_image.size(), CV_8UC1);
        const float min_d = settings_.min_distance_m;
        const float max_d = settings_.max_distance_m;
        for (int y = 0; y < depth_image.rows; ++y) {
            const uint16_t* depth_row = depth_image.ptr<uint16_t>(y);
            uchar* mask_row = depth_mask.ptr<uchar>(y);
            for (int x = 0; x < depth_image.cols; ++x) {
                const float d = depth_row[x] / 1000.0f;
                mask_row[x] = (d >= min_d && d <= max_d) ? 255 : 0;
            }
        }
    }

    // ---- Step 2: HSV conversion (once per frame) ----
    cv::Mat hsv_frame;
    cv::cvtColor(color_image, hsv_frame, cv::COLOR_BGR2HSV);

    // ---- Step 3+4: per-color segmentation and blob extraction ----
    std::vector<Detection> detections;
    const int hue_tol = std::max(0, std::min(90, settings_.hue_tolerance));
    const int sat_min = std::max(0, std::min(255, settings_.sat_minimum));
    const int val_min = std::max(0, std::min(255, settings_.val_minimum));
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));

    for (const auto& profile : color_profiles_) {
        if (!profile.enabled) continue;
        if (profile.avg_hue < 0.0f || profile.avg_saturation < 0.0f) continue;  // Not calibrated

        const int target_hue = static_cast<int>(std::round(profile.avg_hue));

        // Build HSV mask with hue wrap-around handling
        cv::Mat color_mask;
        const int hue_low = target_hue - hue_tol;
        const int hue_high = target_hue + hue_tol;

        if (hue_low < 0) {
            // Wrap around 0 (e.g., red)
            cv::Mat m1, m2;
            cv::inRange(hsv_frame, cv::Scalar(0, sat_min, val_min),
                        cv::Scalar(hue_high, 255, 255), m1);
            cv::inRange(hsv_frame, cv::Scalar(180 + hue_low, sat_min, val_min),
                        cv::Scalar(180, 255, 255), m2);
            cv::bitwise_or(m1, m2, color_mask);
        } else if (hue_high > 180) {
            // Wrap around 180 (e.g., red)
            cv::Mat m1, m2;
            cv::inRange(hsv_frame, cv::Scalar(hue_low, sat_min, val_min),
                        cv::Scalar(180, 255, 255), m1);
            cv::inRange(hsv_frame, cv::Scalar(0, sat_min, val_min),
                        cv::Scalar(hue_high - 180, 255, 255), m2);
            cv::bitwise_or(m1, m2, color_mask);
        } else {
            cv::inRange(hsv_frame, cv::Scalar(hue_low, sat_min, val_min),
                        cv::Scalar(hue_high, 255, 255), color_mask);
        }

        // AND with depth mask: right color AND in the depth range
        cv::Mat combined_mask;
        if (depth_available) {
            cv::bitwise_and(color_mask, depth_mask, combined_mask);
        } else {
            combined_mask = color_mask;
        }

        // Close small holes (LED hotspots can punch holes in the mask)
        cv::morphologyEx(combined_mask, combined_mask, cv::MORPH_CLOSE, kernel);

        // Find blobs
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(combined_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        struct BlobCandidate {
            cv::Rect bbox;
            cv::Point2f center;
            float avg_depth_m;
            float physical_area_cm2;
            double circularity;
            double pixel_area;
        };
        std::vector<BlobCandidate> candidates;

        for (const auto& contour : contours) {
            if (contour.size() < 3) continue;

            const cv::Rect bbox = cv::boundingRect(contour);
            const double pixel_area = cv::contourArea(contour);
            if (pixel_area < 3.0) continue;

            // Median depth over the contour perimeter (fast, robust)
            std::vector<float> depths;
            depths.reserve(contour.size());
            for (const auto& pt : contour) {
                if (pt.x < 0 || pt.x >= depth_image.cols ||
                    pt.y < 0 || pt.y >= depth_image.rows) continue;
                const float d = depth_image.at<uint16_t>(pt.y, pt.x) / 1000.0f;
                if (d > 0.05f) depths.push_back(d);
            }
            if (depths.empty()) continue;
            const auto mid_it = depths.begin() + depths.size() / 2;
            std::nth_element(depths.begin(), mid_it, depths.end());
            const float avg_depth = *mid_it;

            // Depth-aware physical surface area filter
            const float depth_sq = avg_depth * avg_depth;
            const float focal_product = intrinsics.fx * intrinsics.fy;
            const float physical_area_m2 = static_cast<float>(pixel_area) * depth_sq / focal_product;
            const float physical_area_cm2 = physical_area_m2 * 10000.0f;
            if (physical_area_cm2 < settings_.min_area_cm2 || physical_area_cm2 > settings_.max_area_cm2) {
                continue;
            }

            // Circularity filter
            const double perimeter = cv::arcLength(contour, true);
            if (perimeter < 0.01) continue;
            const double circularity = (4.0 * CV_PI * pixel_area) / (perimeter * perimeter);
            if (circularity < settings_.min_circularity) continue;

            BlobCandidate cand;
            cand.bbox = bbox;
            cand.center = cv::Point2f(bbox.x + bbox.width / 2.0f, bbox.y + bbox.height / 2.0f);
            cand.avg_depth_m = avg_depth;
            cand.physical_area_cm2 = physical_area_cm2;
            cand.circularity = circularity;
            cand.pixel_area = pixel_area;
            candidates.push_back(cand);
        }

        // Cap blobs per color: keep the LARGEST ones (most likely real balls)
        if (static_cast<int>(candidates.size()) > settings_.max_balls_per_color) {
            std::sort(candidates.begin(), candidates.end(),
                      [](const BlobCandidate& a, const BlobCandidate& b) {
                          return a.pixel_area > b.pixel_area;
                      });
            candidates.resize(settings_.max_balls_per_color);
        }

        // Left-to-right order for semi-stable per-frame indices
        std::sort(candidates.begin(), candidates.end(),
                  [](const BlobCandidate& a, const BlobCandidate& b) {
                      return a.center.x < b.center.x;
                  });

        for (const auto& cand : candidates) {
            Detection det;
            det.box = cv::Rect_<float>(cand.bbox.x, cand.bbox.y,
                                       cand.bbox.width, cand.bbox.height);
            det.world_pos = deprojectToWorld(cand.center, cand.avg_depth_m, intrinsics);
            det.confidence = 1.0f;
            det.class_id = 0;
            det.color_name = profile.name;  // Pre-identified by the mask itself
            const int cx = cvRound(cand.center.x), cy = cvRound(cand.center.y);
            if (cx >= 0 && cx < color_image.cols && cy >= 0 && cy < color_image.rows) {
                det.detected_bgr_color = color_image.at<cv::Vec3b>(cy, cx);
            }
            detections.push_back(det);

            if (settings_.debug_logging) {
                std::cout << "[ColorOnlyTracker] " << profile.name
                          << " blob at (" << cand.center.x << "," << cand.center.y << ")"
                          << " depth=" << cand.avg_depth_m << "m"
                          << " area=" << cand.physical_area_cm2 << "cm2"
                          << " circ=" << cand.circularity << std::endl;
            }
        }
    }

    // ---- Step 5: emit one SimpleBall per blob (identity-free) ----
    int next_id = 0;
    for (const auto& det : detections) {
        SimpleBall ball;
        ball.id = next_id++;
        ball.color_name = det.color_name;
        ball.position = det.world_pos;
        ball.pixel_pos = cv::Point2f(det.box.x + det.box.width / 2.0f,
                                     det.box.y + det.box.height / 2.0f);
        ball.bbox = det.box;
        ball.state = BallState::IN_FLIGHT;
        ball.previous_state = BallState::IN_FLIGHT;
        ball.is_held = false;
        ball.held_by_hand_id = -1;
        ball.has_yolo_detection = true;   // Marks the ball "seen this frame" for the hub UI
        ball.yolo_confidence = det.confidence;
        ball.color_match_score = 1.0f;
        ball.yolo_class_id = 0;
        ball.detected_bgr_color = det.detected_bgr_color;
        ball.tracking_reason = "color_blob";
        balls.push_back(ball);
    }

    last_balls_ = balls;
    last_detections_ = detections;
    return {balls, events};
}

// ============================================================================
// CALIBRATION
// ============================================================================

bool ColorOnlyTracker::calibrateColor(const std::string& color_name,
                                      cv::Point click_point,
                                      std::string& error_message) {
    // Find the profile
    ColorProfile* profile = nullptr;
    for (auto& p : color_profiles_) {
        if (p.name == color_name) { profile = &p; break; }
    }
    if (!profile) {
        error_message = "Unknown color profile: " + color_name;
        return false;
    }

    if (last_color_image_.empty()) {
        error_message = "No camera frame cached yet - wait for the first frame";
        return false;
    }

    // Sample a 5x5 region around the click and take the median H and S
    const int half = 2;
    std::vector<float> hues, sats;
    for (int dy = -half; dy <= half; ++dy) {
        for (int dx = -half; dx <= half; ++dx) {
            const int x = click_point.x + dx;
            const int y = click_point.y + dy;
            if (x < 0 || x >= last_color_image_.cols || y < 0 || y >= last_color_image_.rows) continue;
            cv::Mat pix = last_color_image_(cv::Rect(x, y, 1, 1));
            cv::Mat hsv_pix;
            cv::cvtColor(pix, hsv_pix, cv::COLOR_BGR2HSV);
            hues.push_back(hsv_pix.at<cv::Vec3b>(0, 0)[0]);
            sats.push_back(hsv_pix.at<cv::Vec3b>(0, 0)[1]);
        }
    }
    if (hues.empty()) {
        error_message = "Click point outside the camera frame";
        return false;
    }

    const auto h_mid = hues.begin() + hues.size() / 2;
    std::nth_element(hues.begin(), h_mid, hues.end());
    const float avg_hue = *h_mid;
    const auto s_mid = sats.begin() + sats.size() / 2;
    std::nth_element(sats.begin(), s_mid, sats.end());
    const float avg_sat = *s_mid;

    // Update in-memory profile
    profile->avg_hue = avg_hue;
    profile->avg_saturation = avg_sat;

    // Persist into the SHARED calibration file (same file the hub UI writes)
    try {
        if (fs::exists(color_profiles_file_)) {
            std::ifstream in(color_profiles_file_);
            json j;
            in >> j;

            if (j.contains("color_profiles")) {
                for (auto& pj : j["color_profiles"]) {
                    if (pj.value("name", std::string("")) == color_name) {
                        pj["avg_hue"] = avg_hue;
                        pj["avg_saturation"] = avg_sat;
                        break;
                    }
                }

                std::ofstream out(color_profiles_file_);
                out << j.dump(4);
                std::cout << "[ColorOnlyTracker] Calibrated '" << color_name
                          << "': H=" << avg_hue << " S=" << avg_sat << std::endl;
                return true;
            }
        }
        // Shared file missing/malformed: profile updated in memory only
        std::cout << "[ColorOnlyTracker] Calibrated '" << color_name
                  << "' (in memory only - shared file not writable)" << std::endl;
        return true;
    } catch (const std::exception& e) {
        error_message = std::string("Failed to save calibration: ") + e.what();
        return false;
    }
}

// ============================================================================
// SETTINGS UPDATES (UDP / ZMQ)
// ============================================================================

bool ColorOnlyTracker::updateSetting(const std::string& key, const std::string& value) {
    auto to_int = [](const std::string& s) { return std::stoi(s); };
    auto to_float = [](const std::string& s) { return std::stof(s); };
    auto to_bool = [](const std::string& s) {
        return s == "1" || s == "true" || s == "TRUE" || s == "True";
    };
    auto clampi = [](int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); };

    try {
        // Hue tolerance (own key + New3D-style alias so the existing UI sliders work)
        if (key == "hue_tolerance" || key == "depth_blob_hue_tolerance") {
            settings_.hue_tolerance = clampi(to_int(value), 1, 90);
        }
        // Saturation minimum
        else if (key == "sat_minimum" || key == "depth_blob_sat_minimum") {
            settings_.sat_minimum = clampi(to_int(value), 0, 255);
        }
        // Value (brightness) minimum
        else if (key == "val_minimum" || key == "depth_blob_val_minimum") {
            settings_.val_minimum = clampi(to_int(value), 0, 255);
        }
        // Depth range (cm)
        else if (key == "min_distance_cm" || key == "depth_blob_min_distance_cm") {
            settings_.min_distance_m = std::max(0.05f, to_float(value) / 100.0f);
        }
        else if (key == "max_distance_cm" || key == "depth_blob_max_distance_cm") {
            settings_.max_distance_m = std::max(settings_.min_distance_m + 0.1f, to_float(value) / 100.0f);
        }
        // Physical area filters (cm^2; the New3D "px" keys are legacy-named but also cm^2)
        else if (key == "min_area_cm2" || key == "depth_blob_min_area_px") {
            settings_.min_area_cm2 = std::max(0.1f, to_float(value));
        }
        else if (key == "max_area_cm2" || key == "depth_blob_max_area_px") {
            settings_.max_area_cm2 = std::max(settings_.min_area_cm2 + 1.0f, to_float(value));
        }
        // Circularity
        else if (key == "min_circularity" || key == "depth_blob_min_circularity") {
            settings_.min_circularity = std::max(0.0f, std::min(1.0f, to_float(value)));
        }
        // Depth filter on/off
        else if (key == "use_depth_filter" || key == "enable_depth_blob_detection") {
            settings_.use_depth_filter = to_bool(value);
        }
        // Max blobs per color
        else if (key == "max_balls_per_color") {
            settings_.max_balls_per_color = clampi(to_int(value), 1, 10);
        }
        // Debug logging
        else if (key == "debug_logging") {
            settings_.debug_logging = to_bool(value);
        }
        // Reload color profiles on demand
        else if (key == "reload_color_profiles") {
            reloadColorProfiles();
        }
        else {
            return false;  // Unknown setting - not handled by this tracker
        }

        saveSettings();
        return true;
    } catch (const std::exception&) {
        return false;  // Bad value format
    }
}

// ============================================================================
// NO-OP INTERFACE METHODS
// ============================================================================

void ColorOnlyTracker::drawHandThresholds(cv::Mat& /*frame*/,
                                          const std::vector<SimpleHand>& /*hands*/,
                                          const CameraIntrinsics& /*intrinsics*/,
                                          const std::vector<SimpleBall>* /*balls_override*/) {
    // No hands tracked - nothing to draw
}

void ColorOnlyTracker::evaluateOverrideCriteria(std::vector<Detection>& /*detections*/,
                                                const cv::Mat& /*color_image*/) {
    // No override logic in identity-free mode
}
