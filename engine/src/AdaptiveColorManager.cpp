#include "AdaptiveColorManager.hpp"
#include "DebugLog.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

namespace juggler {

// ============================================================================
// AdaptiveColorProfile Implementation
// ============================================================================

AdaptiveColorProfile::AdaptiveColorProfile(
    const std::string& name_, bool enabled_,
    const cv::Scalar& min_hsv_, const cv::Scalar& max_hsv_,
    const cv::Scalar& min_hsv2_, const cv::Scalar& max_hsv2_)
    : name(name_)
    , enabled(enabled_)
    , min_hsv(min_hsv_)
    , max_hsv(max_hsv_)
    , min_hsv2(min_hsv2_)
    , max_hsv2(max_hsv2_)
    , center_hue((min_hsv_[0] + max_hsv_[0]) / 2.0f)
    , hue_range_width(max_hsv_[0] - min_hsv_[0])
    , frames_tracked(0)
    , frames_unmatched(0)
    , success_rate(0.0f)
    , min_hue_width(10.0f)
    , max_hue_width(40.0f)
    , mean_observed_hue(center_hue)
{
}

// ============================================================================
// AdaptiveColorManager Implementation
// ============================================================================

AdaptiveColorManager::AdaptiveColorManager(const AdaptationConfig& config)
    : config_(config)
    , frame_count_(0)
{
    INFO_LOG("AdaptiveColorManager: Initialized with adaptive color tracking");
}

void AdaptiveColorManager::initializeFromProfiles(
    const std::vector<cv::Scalar>& min_hsv_values,
    const std::vector<cv::Scalar>& max_hsv_values,
    const std::vector<std::string>& color_names,
    const std::vector<bool>& enabled_states)
{
    adaptive_profiles_.clear();
    
    for (size_t i = 0; i < color_names.size(); ++i) {
        AdaptiveColorProfile profile(
            color_names[i],
            enabled_states[i],
            min_hsv_values[i],
            max_hsv_values[i]
        );
        
        // Set constraints
        profile.min_hue_width = config_.min_range_width;
        profile.max_hue_width = config_.max_range_width;
        
        adaptive_profiles_.push_back(profile);
        
        INFO_LOG("AdaptiveColorManager: Initialized profile '", color_names[i],
                 "' - center_hue=", profile.center_hue,
                 " width=", profile.hue_range_width,
                 " enabled=", enabled_states[i]);
    }
}

void AdaptiveColorManager::monitorFrame(
    const std::map<std::string, int>& matched_colors,
    const std::vector<float>& detection_hues)
{
    if (!config_.enabled) return;
    
    frame_count_++;
    
    for (auto& profile : adaptive_profiles_) {
        if (!profile.enabled) continue;
        
        // Check if this color was matched this frame
        bool matched_this_frame = (matched_colors.count(profile.name) > 0);
        
        // Update rolling history
        profile.recent_matches.push_back(matched_this_frame);
        if (profile.recent_matches.size() > static_cast<size_t>(config_.history_window_size)) {
            profile.recent_matches.pop_front();
        }
        
        // Update consecutive counters
        if (matched_this_frame) {
            profile.frames_tracked++;
            profile.frames_unmatched = 0;
            
            // Record observed hue value
            int detection_idx = matched_colors.at(profile.name);
            if (detection_idx >= 0 && detection_idx < static_cast<int>(detection_hues.size())) {
                float observed_hue = detection_hues[detection_idx];
                profile.observed_hues.push_back(observed_hue);
                
                // Keep only last 30 observations
                if (profile.observed_hues.size() > 30) {
                    profile.observed_hues.erase(profile.observed_hues.begin());
                }
                
                // Update mean
                if (!profile.observed_hues.empty()) {
                    profile.mean_observed_hue = calculateMean(profile.observed_hues);
                }
            }
        } else {
            profile.frames_unmatched++;
            profile.frames_tracked = 0;
        }
        
        // Calculate success rate
        if (!profile.recent_matches.empty()) {
            int successes = std::count(profile.recent_matches.begin(),
                                      profile.recent_matches.end(), true);
            profile.success_rate = static_cast<float>(successes) / 
                                  profile.recent_matches.size();
        }
    }
}

void AdaptiveColorManager::adjustRanges()
{
    if (!config_.enabled) return;
    
    // Only adjust every N frames to avoid instability
    if (frame_count_ % config_.frames_between_adjustments != 0) {
        return;
    }
    
    INFO_LOG("AdaptiveColorManager: Adjusting ranges at frame ", frame_count_);
    
    // Step 1: Identify colors needing adjustment
    std::vector<AdaptiveColorProfile*> failing_colors;
    std::vector<AdaptiveColorProfile*> succeeding_colors;
    
    for (auto& profile : adaptive_profiles_) {
        if (!profile.enabled) continue;
        
        // Need sufficient history before making adjustments
        if (profile.recent_matches.size() < static_cast<size_t>(config_.history_window_size / 2)) {
            continue;
        }
        
        if (profile.success_rate < config_.failure_threshold) {
            failing_colors.push_back(&profile);
            INFO_LOG("  ", profile.name, " is FAILING (success rate: ",
                     profile.success_rate * 100, "%)");
        } else if (profile.success_rate > config_.success_threshold) {
            succeeding_colors.push_back(&profile);
            INFO_LOG("  ", profile.name, " is SUCCEEDING (success rate: ",
                     profile.success_rate * 100, "%)");
        }
    }
    
    // Step 2: Expand ranges for failing colors
    for (auto* profile : failing_colors) {
        expandRange(*profile);
        
        // If we have observed hues, shift center toward them
        if (!profile->observed_hues.empty()) {
            shiftCenterToward(*profile, profile->mean_observed_hue);
        }
    }
    
    // Step 3: Contract ranges for succeeding colors
    for (auto* profile : succeeding_colors) {
        contractRange(*profile);
        
        // Shift center toward mean of observed hues
        if (!profile->observed_hues.empty()) {
            profile->center_hue = profile->mean_observed_hue;
            updateRangeFromCenter(*profile);
            INFO_LOG("  Centered ", profile->name, " at observed mean hue ",
                     profile->mean_observed_hue);
        }
    }
    
    // Step 4: Resolve conflicts (overlapping ranges)
    if (!failing_colors.empty() || !succeeding_colors.empty()) {
        resolveConflicts(failing_colors, succeeding_colors);
    }
}

void AdaptiveColorManager::expandRange(AdaptiveColorProfile& profile)
{
    float old_width = profile.hue_range_width;
    
    // Increase range width
    profile.hue_range_width += config_.expansion_step;
    
    // Clamp to maximum
    if (profile.hue_range_width > profile.max_hue_width) {
        profile.hue_range_width = profile.max_hue_width;
    }
    
    // Update min/max from center and width
    updateRangeFromCenter(profile);
    
    INFO_LOG("AdaptiveColorManager: Expanded ", profile.name, " range from ",
             old_width, " to ", profile.hue_range_width, " degrees");
}

void AdaptiveColorManager::contractRange(AdaptiveColorProfile& profile)
{
    float old_width = profile.hue_range_width;
    
    // Decrease range width
    profile.hue_range_width -= config_.contraction_step;
    
    // Clamp to minimum
    if (profile.hue_range_width < profile.min_hue_width) {
        profile.hue_range_width = profile.min_hue_width;
    }
    
    // Update min/max from center and width
    updateRangeFromCenter(profile);
    
    INFO_LOG("AdaptiveColorManager: Contracted ", profile.name, " range from ",
             old_width, " to ", profile.hue_range_width, " degrees");
}

void AdaptiveColorManager::shiftCenterToward(AdaptiveColorProfile& profile, float target_hue)
{
    float old_center = profile.center_hue;
    
    // Calculate direction to shift
    float distance = hueDistance(profile.center_hue, target_hue);
    
    // Determine shift direction (shortest path on hue circle)
    float shift_amount = config_.shift_step;
    if (distance < 0) {
        shift_amount = -shift_amount;
    }
    
    // Apply shift
    profile.center_hue += shift_amount;
    profile.center_hue = normalizeHue(profile.center_hue);
    
    // Update min/max from new center
    updateRangeFromCenter(profile);
    
    INFO_LOG("AdaptiveColorManager: Shifted ", profile.name, " center from ",
             old_center, " to ", profile.center_hue, " (target: ", target_hue, ")");
}

void AdaptiveColorManager::updateRangeFromCenter(AdaptiveColorProfile& profile)
{
    float half_width = profile.hue_range_width / 2.0f;
    
    float min_hue = profile.center_hue - half_width;
    float max_hue = profile.center_hue + half_width;
    
    // Handle wrap-around for hue values
    if (min_hue < 0) {
        // Wrap around: use two ranges
        profile.min_hsv[0] = 0;
        profile.max_hsv[0] = max_hue;
        profile.min_hsv2[0] = 180 + min_hue;  // min_hue is negative
        profile.max_hsv2[0] = 180;
    } else if (max_hue > 180) {
        // Wrap around: use two ranges
        profile.min_hsv[0] = min_hue;
        profile.max_hsv[0] = 180;
        profile.min_hsv2[0] = 0;
        profile.max_hsv2[0] = max_hue - 180;
    } else {
        // Normal case: single range
        profile.min_hsv[0] = min_hue;
        profile.max_hsv[0] = max_hue;
        profile.min_hsv2[0] = -1;  // Disable secondary range
        profile.max_hsv2[0] = -1;
    }
}

float AdaptiveColorManager::calculateOverlap(
    const AdaptiveColorProfile& profile1,
    const AdaptiveColorProfile& profile2) const
{
    // Calculate overlap between two hue ranges
    // This is complex due to wrap-around, so we use a simplified approach
    
    float dist = std::abs(hueDistance(profile1.center_hue, profile2.center_hue));
    float combined_half_width = (profile1.hue_range_width + profile2.hue_range_width) / 2.0f;
    
    float overlap = combined_half_width - dist;
    return std::max(0.0f, overlap);
}

void AdaptiveColorManager::resolveConflicts(
    const std::vector<AdaptiveColorProfile*>& failing_colors,
    const std::vector<AdaptiveColorProfile*>& succeeding_colors)
{
    // For each failing color, check if it overlaps with succeeding colors
    for (auto* failing : failing_colors) {
        for (auto* succeeding : succeeding_colors) {
            float overlap = calculateOverlap(*failing, *succeeding);
            
            if (overlap > 0) {
                // Move succeeding color away from failing color
                // This gives failing color more "space" to expand
                float distance = hueDistance(failing->center_hue, succeeding->center_hue);
                float shift_direction = (distance > 0) ? 1.0f : -1.0f;
                
                succeeding->center_hue += shift_direction * config_.shift_step;
                succeeding->center_hue = normalizeHue(succeeding->center_hue);
                
                updateRangeFromCenter(*succeeding);
                
                INFO_LOG("AdaptiveColorManager: Shifted ", succeeding->name,
                         " away from ", failing->name, " to reduce overlap");
            }
        }
    }
    
    // Ensure minimum separation between all enabled colors
    ensureMinimumSeparation();
}

void AdaptiveColorManager::ensureMinimumSeparation()
{
    // Sort profiles by center hue
    std::vector<AdaptiveColorProfile*> sorted_profiles;
    for (auto& profile : adaptive_profiles_) {
        if (profile.enabled) {
            sorted_profiles.push_back(&profile);
        }
    }
    
    if (sorted_profiles.size() < 2) return;
    
    std::sort(sorted_profiles.begin(), sorted_profiles.end(),
              [](const AdaptiveColorProfile* a, const AdaptiveColorProfile* b) {
                  return a->center_hue < b->center_hue;
              });
    
    // Check adjacent pairs for minimum separation
    for (size_t i = 0; i < sorted_profiles.size() - 1; ++i) {
        auto* current = sorted_profiles[i];
        auto* next = sorted_profiles[i + 1];
        
        float separation = next->center_hue - current->center_hue;
        
        if (separation < config_.min_separation) {
            // Push them apart equally
            float adjustment = (config_.min_separation - separation) / 2.0f;
            current->center_hue -= adjustment;
            next->center_hue += adjustment;
            
            current->center_hue = normalizeHue(current->center_hue);
            next->center_hue = normalizeHue(next->center_hue);
            
            updateRangeFromCenter(*current);
            updateRangeFromCenter(*next);
            
            INFO_LOG("AdaptiveColorManager: Enforced minimum separation between ",
                     current->name, " and ", next->name);
        }
    }
}

const AdaptiveColorProfile* AdaptiveColorManager::getProfile(const std::string& color_name) const
{
    for (const auto& profile : adaptive_profiles_) {
        if (profile.name == color_name) {
            return &profile;
        }
    }
    return nullptr;
}

void AdaptiveColorManager::setProfileEnabled(const std::string& color_name, bool enabled)
{
    for (auto& profile : adaptive_profiles_) {
        if (profile.name == color_name) {
            profile.enabled = enabled;
            INFO_LOG("AdaptiveColorManager: ", (enabled ? "Enabled" : "Disabled"),
                     " profile '", color_name, "'");
            return;
        }
    }
}

void AdaptiveColorManager::reset()
{
    frame_count_ = 0;
    
    for (auto& profile : adaptive_profiles_) {
        profile.recent_matches.clear();
        profile.frames_tracked = 0;
        profile.frames_unmatched = 0;
        profile.success_rate = 0.0f;
        profile.observed_hues.clear();
        
        // Reset to initial range
        profile.center_hue = (profile.min_hsv[0] + profile.max_hsv[0]) / 2.0f;
        profile.hue_range_width = profile.max_hsv[0] - profile.min_hsv[0];
    }
    
    INFO_LOG("AdaptiveColorManager: Reset all profiles to initial state");
}

std::map<std::string, float> AdaptiveColorManager::getSuccessRates() const
{
    std::map<std::string, float> rates;
    for (const auto& profile : adaptive_profiles_) {
        if (profile.enabled) {
            rates[profile.name] = profile.success_rate;
        }
    }
    return rates;
}

float AdaptiveColorManager::calculateMean(const std::vector<float>& values) const
{
    if (values.empty()) return 0.0f;
    
    float sum = std::accumulate(values.begin(), values.end(), 0.0f);
    return sum / values.size();
}

float AdaptiveColorManager::normalizeHue(float hue) const
{
    while (hue < 0) hue += 180;
    while (hue >= 180) hue -= 180;
    return hue;
}

float AdaptiveColorManager::hueDistance(float hue1, float hue2) const
{
    // Calculate shortest angular distance on hue circle (0-180)
    float diff = hue2 - hue1;
    
    // Normalize to [-90, 90] range (shortest path)
    if (diff > 90) diff -= 180;
    if (diff < -90) diff += 180;
    
    return diff;
}

} // namespace juggler