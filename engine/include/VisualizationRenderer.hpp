#pragma once

#include "IBallTracker.hpp"
#include "juggler.pb.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <map>

// Forward declarations
struct RecordingFrame;
struct CameraIntrinsics;
struct TrackedHand;
struct Detection;
struct SimpleBall;
struct SimpleHand;
struct BallEvent;

class VisualizationRenderer {
public:
    VisualizationRenderer();
    ~VisualizationRenderer();

    // Main rendering function
    cv::Mat renderVisualizationsOnFrame(const cv::Mat& frame,
                                       const RecordingFrame& rec_frame,
                                       const CameraIntrinsics& camera_intrinsics,
                                       const juggler::v1::VisualizationStates& viz_states,
                                       bool record_with_yolo_boxes,
                                       IBallTracker* tracker);

private:
    // Helper methods for specific visualizations
    void drawRawDetections(cv::Mat& image,
                          const std::vector<Detection>& detections,
                          const cv::Mat& original_frame,
                          std::vector<std::string>& info_lines,
                          std::vector<cv::Scalar>& info_colors);
    
    void drawFilteredDetections(cv::Mat& image,
                               const std::vector<Detection>& detections,
                               const cv::Mat& original_frame,
                               std::vector<std::string>& info_lines,
                               std::vector<cv::Scalar>& info_colors);
    
    void drawYoloColorCalibration(cv::Mat& image,
                                  const std::vector<Detection>& detections);
    
    void drawHandTracking(cv::Mat& image,
                         const std::vector<TrackedHand>& hands,
                         const CameraIntrinsics& intrinsics,
                         bool show_skeleton);
    
    void drawColorTrackedBalls(cv::Mat& image,
                              const std::vector<SimpleBall>& balls,
                              const std::vector<SimpleHand>& hands,
                              const CameraIntrinsics& intrinsics,
                              std::vector<std::string>& info_lines,
                              std::vector<cv::Scalar>& info_colors,
                              IBallTracker* tracker);
    
    void drawFinalTrackers(cv::Mat& image,
                          const std::vector<SimpleBall>& balls);
    
    void drawTrajectory(cv::Mat& image,
                       const std::vector<SimpleBall>& balls,
                       const CameraIntrinsics& intrinsics,
                       std::vector<std::string>& info_lines,
                       std::vector<cv::Scalar>& info_colors,
                       IBallTracker* tracker);
    
    void drawHandThresholds(cv::Mat& image,
                           const std::vector<SimpleHand>& hands,
                           const CameraIntrinsics& intrinsics,
                           IBallTracker* tracker);
    
    void drawHandVelocityZone(cv::Mat& image,
                             const std::vector<SimpleHand>& hands,
                             const std::vector<SimpleBall>& balls,
                             const CameraIntrinsics& intrinsics,
                             IBallTracker* tracker);
    
    void drawTrajectoryPredictions(cv::Mat& image,
                                  const std::vector<SimpleBall>& balls,
                                  const CameraIntrinsics& intrinsics,
                                  std::vector<std::string>& info_lines,
                                  std::vector<cv::Scalar>& info_colors,
                                  IBallTracker* tracker);
    
    void drawThrowCatchEvents(const std::vector<BallEvent>& events,
                             const std::vector<SimpleBall>& balls,
                             const std::vector<SimpleHand>& hands,
                             std::vector<std::string>& info_lines,
                             std::vector<cv::Scalar>& info_colors,
                             IBallTracker* tracker);
    
    cv::Mat createInfoPanel(const cv::Mat& frame,
                           const std::vector<std::string>& info_lines,
                           const std::vector<cv::Scalar>& info_colors);
    
    std::vector<std::string> wrapText(const std::string& text,
                                     int max_width,
                                     int font_face,
                                     float font_scale,
                                     int font_thickness);
    
    std::map<std::string, cv::Scalar> loadColorProfiles();
};