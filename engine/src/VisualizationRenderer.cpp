#include "VisualizationRenderer.hpp"
#include "DebugLog.hpp"
#include "json.hpp"
#include <fstream>

extern void writeDebugLog(const std::string& message);

VisualizationRenderer::VisualizationRenderer() {
}

VisualizationRenderer::~VisualizationRenderer() {
}

// NOTE: The actual renderVisualizationsOnFrame implementation is in Engine.cpp
// This class currently serves as a placeholder for future refactoring
// The RecordingManager calls the Engine.cpp version via external linkage

cv::Mat VisualizationRenderer::renderVisualizationsOnFrame(const cv::Mat& frame,
                                                          const RecordingFrame& rec_frame,
                                                          const CameraIntrinsics& camera_intrinsics,
                                                          const juggler::v1::VisualizationStates& viz_states,
                                                          bool record_with_yolo_boxes,
                                                          IBallTracker* tracker) {
    // This should not be called - RecordingManager uses the Engine.cpp version
    writeDebugLog("ERROR: VisualizationRenderer::renderVisualizationsOnFrame stub called!");
    (void)rec_frame;
    (void)camera_intrinsics;
    (void)viz_states;
    (void)record_with_yolo_boxes;
    (void)tracker;
    return frame.clone();
}

// Stub implementations for helper methods
void VisualizationRenderer::drawRawDetections(cv::Mat& image,
                                             const std::vector<Detection>& detections,
                                             const cv::Mat& original_frame,
                                             std::vector<std::string>& info_lines,
                                             std::vector<cv::Scalar>& info_colors) {
    // Implementation will be extracted from Engine.cpp
}

void VisualizationRenderer::drawFilteredDetections(cv::Mat& image,
                                                  const std::vector<Detection>& detections,
                                                  const cv::Mat& original_frame,
                                                  std::vector<std::string>& info_lines,
                                                  std::vector<cv::Scalar>& info_colors) {
    // Implementation will be extracted from Engine.cpp
}

void VisualizationRenderer::drawYoloColorCalibration(cv::Mat& image,
                                                    const std::vector<Detection>& detections) {
    // Implementation will be extracted from Engine.cpp
}

void VisualizationRenderer::drawHandTracking(cv::Mat& image,
                                            const std::vector<TrackedHand>& hands,
                                            const CameraIntrinsics& intrinsics,
                                            bool show_skeleton) {
    // Implementation will be extracted from Engine.cpp
}

void VisualizationRenderer::drawColorTrackedBalls(cv::Mat& image,
                                                 const std::vector<SimpleBall>& balls,
                                                 const std::vector<SimpleHand>& hands,
                                                 const CameraIntrinsics& intrinsics,
                                                 std::vector<std::string>& info_lines,
                                                 std::vector<cv::Scalar>& info_colors,
                                                 IBallTracker* tracker) {
    // Implementation will be extracted from Engine.cpp
}

void VisualizationRenderer::drawFinalTrackers(cv::Mat& image,
                                             const std::vector<SimpleBall>& balls) {
    // Implementation will be extracted from Engine.cpp
}

void VisualizationRenderer::drawTrajectory(cv::Mat& image,
                                          const std::vector<SimpleBall>& balls,
                                          const CameraIntrinsics& intrinsics,
                                          std::vector<std::string>& info_lines,
                                          std::vector<cv::Scalar>& info_colors,
                                          IBallTracker* tracker) {
    // Implementation will be extracted from Engine.cpp
}

void VisualizationRenderer::drawHandThresholds(cv::Mat& image,
                                              const std::vector<SimpleHand>& hands,
                                              const CameraIntrinsics& intrinsics,
                                              IBallTracker* tracker) {
    // Implementation will be extracted from Engine.cpp
}

void VisualizationRenderer::drawHandVelocityZone(cv::Mat& image,
                                                const std::vector<SimpleHand>& hands,
                                                const std::vector<SimpleBall>& balls,
                                                const CameraIntrinsics& intrinsics,
                                                IBallTracker* tracker) {
    // Implementation will be extracted from Engine.cpp
}

void VisualizationRenderer::drawTrajectoryPredictions(cv::Mat& image,
                                                     const std::vector<SimpleBall>& balls,
                                                     const CameraIntrinsics& intrinsics,
                                                     std::vector<std::string>& info_lines,
                                                     std::vector<cv::Scalar>& info_colors,
                                                     IBallTracker* tracker) {
    // Implementation will be extracted from Engine.cpp
}

void VisualizationRenderer::drawThrowCatchEvents(const std::vector<BallEvent>& events,
                                                const std::vector<SimpleBall>& balls,
                                                const std::vector<SimpleHand>& hands,
                                                std::vector<std::string>& info_lines,
                                                std::vector<cv::Scalar>& info_colors,
                                                IBallTracker* tracker) {
    // Implementation will be extracted from Engine.cpp
}

cv::Mat VisualizationRenderer::createInfoPanel(const cv::Mat& frame,
                                               const std::vector<std::string>& info_lines,
                                               const std::vector<cv::Scalar>& info_colors) {
    // Implementation will be extracted from Engine.cpp
    return frame.clone();
}

std::vector<std::string> VisualizationRenderer::wrapText(const std::string& text,
                                                         int max_width,
                                                         int font_face,
                                                         float font_scale,
                                                         int font_thickness) {
    // Implementation will be extracted from Engine.cpp
    return {text};
}

std::map<std::string, cv::Scalar> VisualizationRenderer::loadColorProfiles() {
    std::map<std::string, cv::Scalar> color_map;
    try {
        std::ifstream color_file("hub/config/color_profiles.json");
        if (color_file.is_open()) {
            nlohmann::json color_profiles;
            color_file >> color_profiles;
            
            if (color_profiles.contains("profiles") && color_profiles["profiles"].is_array()) {
                for (const auto& profile : color_profiles["profiles"]) {
                    std::string name = profile["name"];
                    std::vector<int> rgb = profile["rgb"];
                    // Convert RGB to BGR for OpenCV
                    color_map[name] = cv::Scalar(rgb[2], rgb[1], rgb[0]);
                }
            }
        }
    } catch (...) {
        // If loading fails, return empty map
    }
    return color_map;
}