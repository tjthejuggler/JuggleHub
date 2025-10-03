#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <sstream>
#include <iomanip>

// Forward declarations
namespace juggler {
    class BallRegistry;
    class SkinToneFilter;
    struct ActiveBall;
}

struct TrackedHand;
struct CameraIntrinsics;

namespace juggler {

/**
 * @brief Multi-factor confidence score for ball detections
 * 
 * Each detection is scored across multiple dimensions to determine
 * if it's truly a ball or a false positive. Individual scores are
 * combined with configurable weights to produce a total confidence.
 */
struct DetectionConfidence {
    // Individual scores (0-1 range, higher = more confident)
    float color_match_score;      ///< How well does color match all samples?
    float shape_score;            ///< Is it circular?
    float size_score;             ///< Is it ball-sized?
    float texture_score;          ///< Is it uniform color?
    float temporal_score;         ///< Was it here before?
    float skin_rejection_score;   ///< Is it NOT skin?
    float dnn_confidence;         ///< DNN detection confidence
    float spatial_consistency;    ///< Does position make sense?
    
    // Configurable weights (should sum to 1.0)
    static constexpr float COLOR_WEIGHT = 0.30f;
    static constexpr float SHAPE_WEIGHT = 0.15f;
    static constexpr float SIZE_WEIGHT = 0.10f;
    static constexpr float TEXTURE_WEIGHT = 0.10f;
    static constexpr float TEMPORAL_WEIGHT = 0.15f;
    static constexpr float SKIN_REJECTION_WEIGHT = 0.10f;
    static constexpr float DNN_WEIGHT = 0.10f;
    
    DetectionConfidence()
        : color_match_score(0.0f), shape_score(0.0f), size_score(0.0f),
          texture_score(0.0f), temporal_score(0.0f), skin_rejection_score(1.0f),
          dnn_confidence(0.0f), spatial_consistency(0.0f) {}
    
    /**
     * @brief Compute total weighted confidence score
     * @return Total confidence (0-1)
     */
    float total() const {
        return (color_match_score * COLOR_WEIGHT +
                shape_score * SHAPE_WEIGHT +
                size_score * SIZE_WEIGHT +
                texture_score * TEXTURE_WEIGHT +
                temporal_score * TEMPORAL_WEIGHT +
                skin_rejection_score * SKIN_REJECTION_WEIGHT +
                dnn_confidence * DNN_WEIGHT);
    }
    
    /**
     * @brief Get a debug string representation
     * @return Human-readable confidence breakdown
     */
    std::string toString() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Confidence: " << total() << " [";
        oss << "Color:" << color_match_score << " ";
        oss << "Shape:" << shape_score << " ";
        oss << "Size:" << size_score << " ";
        oss << "Texture:" << texture_score << " ";
        oss << "Temporal:" << temporal_score << " ";
        oss << "SkinRej:" << skin_rejection_score << " ";
        oss << "DNN:" << dnn_confidence << "]";
        return oss.str();
    }
};

/**
 * @brief Computes multi-factor confidence scores for ball detections
 * 
 * The ConfidenceScorer evaluates detections across multiple dimensions:
 * - Color matching (against all calibration samples)
 * - Shape validation (circularity)
 * - Size validation (expected ball diameter)
 * - Texture uniformity
 * - Temporal consistency (position history)
 * - Skin tone rejection
 * - DNN confidence
 */
class ConfidenceScorer {
public:
    ConfidenceScorer(const BallRegistry& registry, const SkinToneFilter& skin_filter);
    ~ConfidenceScorer() = default;
    
    /**
     * @brief Compute confidence for a detection matching a specific ball
     * @param hsv_frame HSV-converted frame
     * @param depth_frame Depth frame (uint16_t, millimeters)
     * @param center Center point of detection
     * @param ball Ball to match against
     * @param hands Tracked hands (for skin rejection)
     * @param intrinsics Camera intrinsics
     * @param dnn_confidence DNN detection confidence (0-1)
     * @return Confidence score breakdown
     */
    DetectionConfidence computeConfidence(
        const cv::Mat& hsv_frame,
        const cv::Mat& depth_frame,
        const cv::Point2f& center,
        const ActiveBall& ball,
        const std::vector<TrackedHand>& hands,
        const CameraIntrinsics& intrinsics,
        float dnn_confidence = 0.0f
    );
    
    /**
     * @brief Set minimum confidence threshold for accepting detections
     * @param threshold Threshold (0-1)
     */
    void setMinConfidenceThreshold(float threshold) { min_confidence_threshold_ = threshold; }
    
    /**
     * @brief Get minimum confidence threshold
     * @return Threshold (0-1)
     */
    float getMinConfidenceThreshold() const { return min_confidence_threshold_; }
    
private:
    const BallRegistry& registry_;
    const SkinToneFilter& skin_filter_;
    float min_confidence_threshold_;
    
    // Individual scoring functions
    
    /**
     * @brief Score how well the detection matches the ball's color profile
     * @param hsv_frame HSV frame
     * @param center Detection center
     * @param ball Ball to match
     * @return Color match score (0-1)
     */
    float scoreColorMatch(const cv::Mat& hsv_frame, const cv::Point2f& center,
                         const ActiveBall& ball);
    
    /**
     * @brief Score the circularity of the detection
     * @param hsv_frame HSV frame
     * @param center Detection center
     * @param ball Ball to match (for color filtering)
     * @return Shape score (0-1)
     */
    float scoreShape(const cv::Mat& hsv_frame, const cv::Point2f& center,
                    const ActiveBall& ball);
    
    /**
     * @brief Score if the detection is the expected ball size
     * @param depth_frame Depth frame
     * @param center Detection center
     * @param ball Ball with expected diameter
     * @param intrinsics Camera intrinsics
     * @return Size score (0-1)
     */
    float scoreSize(const cv::Mat& depth_frame, const cv::Point2f& center,
                   const ActiveBall& ball, const CameraIntrinsics& intrinsics);
    
    /**
     * @brief Score the color uniformity (texture) of the detection
     * @param hsv_frame HSV frame
     * @param center Detection center
     * @return Texture score (0-1)
     */
    float scoreTexture(const cv::Mat& hsv_frame, const cv::Point2f& center);
    
    /**
     * @brief Score temporal consistency (was ball here before?)
     * @param center Detection center
     * @param ball Ball with position history
     * @return Temporal score (0-1)
     */
    float scoreTemporal(const cv::Point2f& center, const ActiveBall& ball);
    
    /**
     * @brief Helper: Check if a region matches the ball's color profile
     * @param hsv_frame HSV frame
     * @param center Center point
     * @param ball Ball to match
     * @param radius Sample radius
     * @return true if matches
     */
    bool matchesBallColor(const cv::Mat& hsv_frame, const cv::Point2f& center,
                         const ActiveBall& ball, int radius = 10);
    
    /**
     * @brief Helper: Compute circularity of a contour (1.0 = perfect circle)
     * @param contour Contour points
     * @return Circularity (0-1)
     */
    float computeCircularity(const std::vector<cv::Point>& contour);
};

} // namespace juggler