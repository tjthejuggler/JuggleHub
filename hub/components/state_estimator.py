import numpy as np
from components.managed_ball import PhysicalState, DetectionState

class ProbabilisticStateEstimator:
    """
    Fuses evidence from physics, proximity, and visibility to estimate
    the physical state of each juggling ball.
    """

    def __init__(self, config):
        self.config = config

    def _normalize_distance(self, distance, max_distance):
        """Normalizes distance to a score from 0.0 to 1.0 (closer is higher)."""
        return max(0.0, 1.0 - (distance / max_distance))

    def estimate_state(self, managed_balls, hand_positions):
        """
        Estimates the physical state for a list of managed balls.

        Args:
            managed_balls (dict): A dictionary of ManagedBall objects.
            hand_positions (dict): A dictionary of hand positions (as numpy arrays).
        """
        # Proximity threshold for instant hand association (in meters)
        instant_catch_distance = self.config.get('instant_catch_distance', 0.15)
        
        for ball in managed_balls.values():
            # --- Calculate Evidence Scores ---
            
            # 1. Physics Score (normalized innovation)
            # This is a placeholder; a proper normalization scheme is needed.
            if hasattr(ball.kf, 'y') and ball.kf.y is not None:
                physics_score = min(1.0, np.linalg.norm(ball.kf.y) / self.config.get('max_innovation', 10.0))
            else:
                physics_score = 0.5  # Default score if innovation is not available

            # 2. Proximity Scores
            left_hand_pos = hand_positions.get('left')
            right_hand_pos = hand_positions.get('right')
            
            proximity_score_left = 0.0
            dist_to_left = float('inf')
            if left_hand_pos is not None:
                dist_to_left = np.linalg.norm(ball.smoothed_position_3d - left_hand_pos)
                proximity_score_left = self._normalize_distance(dist_to_left, self.config.get('max_proximity_distance', 0.5))

            proximity_score_right = 0.0
            dist_to_right = float('inf')
            if right_hand_pos is not None:
                dist_to_right = np.linalg.norm(ball.smoothed_position_3d - right_hand_pos)
                proximity_score_right = self._normalize_distance(dist_to_right, self.config.get('max_proximity_distance', 0.5))

            # 3. Visibility Score
            visibility_score = 1.0 if ball.detection_state == DetectionState.DETECTED else 0.0

            # --- CRITICAL: Instant Catch Logic ---
            # If ball was recently detected near a hand and now becomes undetected,
            # instantly associate it with that hand (it's been caught/occluded)
            if ball.detection_state == DetectionState.UNDETECTED:
                # Check if ball is very close to either hand
                if dist_to_left < instant_catch_distance:
                    # Ball is very close to left hand and not detected - it must be held
                    ball.physical_state = PhysicalState.HELD_LEFT
                    ball.confidences = {
                        'held_left': 1.0,
                        'held_right': 0.0,
                        'unheld': 0.0
                    }
                    continue  # Skip normal state estimation
                elif dist_to_right < instant_catch_distance:
                    # Ball is very close to right hand and not detected - it must be held
                    ball.physical_state = PhysicalState.HELD_RIGHT
                    ball.confidences = {
                        'held_left': 0.0,
                        'held_right': 1.0,
                        'unheld': 0.0
                    }
                    continue  # Skip normal state estimation

            # --- Fuse Evidence (Normal Case) ---
            w_physics = self.config.get('w_physics', 0.5)
            w_proximity = self.config.get('w_proximity', 0.4)
            
            confidence_held_left = (w_physics * physics_score) + (w_proximity * proximity_score_left)
            confidence_held_right = (w_physics * physics_score) + (w_proximity * proximity_score_right)
            confidence_unheld = (w_physics * (1.0 - physics_score)) + \
                                (w_proximity * (1.0 - max(proximity_score_left, proximity_score_right)))

            # Apply visibility bonus for occlusion near hands
            if visibility_score == 0.0:
                if proximity_score_left > 0.8:
                    confidence_held_left += 0.5
                if proximity_score_right > 0.8:
                    confidence_held_right += 0.5
            
            ball.confidences = {
                'held_left': confidence_held_left,
                'held_right': confidence_held_right,
                'unheld': confidence_unheld
            }

            # --- Apply Hysteresis ---
            # This is a simplified implementation. A more robust state machine would be needed.
            max_confidence_state = max(ball.confidences, key=ball.confidences.get)
            
            if max_confidence_state == ball.potential_next_state:
                ball.frames_in_potential_state += 1
            else:
                ball.potential_next_state = max_confidence_state
                ball.frames_in_potential_state = 1
            
            # Reduce hysteresis for instant catches (when ball is undetected near hand)
            hysteresis_threshold = self.config.get('hysteresis_frames', 3)
            if ball.detection_state == DetectionState.UNDETECTED and \
               (proximity_score_left > 0.8 or proximity_score_right > 0.8):
                hysteresis_threshold = 1  # Instant transition
            
            if ball.frames_in_potential_state >= hysteresis_threshold:
                if max_confidence_state == 'held_left':
                    ball.physical_state = PhysicalState.HELD_LEFT
                elif max_confidence_state == 'held_right':
                    ball.physical_state = PhysicalState.HELD_RIGHT
                else:
                    ball.physical_state = PhysicalState.UNHELD