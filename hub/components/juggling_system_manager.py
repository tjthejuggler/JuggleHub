import logging
import numpy as np
from components.color_profiler import ColorProfileManager
from components.ball_identifier import BallIdentifier
from components.kalman_tracker import KalmanBallTracker
from components.state_estimator import ProbabilisticStateEstimator
from components.managed_ball import ManagedBall, PhysicalState

logger = logging.getLogger(__name__)

class JugglingSystemManager:
    """
    Orchestrates all components of the juggling tracking system.
    """

    def __init__(self, config):
        self.color_profile_manager = ColorProfileManager()
        self.color_profile_manager.load_profiles()
        self.ball_identifier = BallIdentifier(self.color_profile_manager)
        self.kalman_tracker = KalmanBallTracker()
        self.state_estimator = ProbabilisticStateEstimator(config)
        self.managed_balls = {}

    def process_frame(self, frame_data, frame_image):
        """
        Processes a single frame of data from the C++ engine.

        Args:
            frame_data (FrameData): The protobuf message from the engine.
            frame_image (np.ndarray): The BGR image frame.

        Returns:
            dict: A dictionary of fully updated ManagedBall objects.
        """
        raw_ball_detections = frame_data.balls
        hand_positions = {hand.side: hand.position_3d for hand in frame_data.hands}

        logger.debug(f"Processing frame with {len(raw_ball_detections)} raw detections")

        # --- Kalman Update ---
        tracked_balls = self.kalman_tracker.update(raw_ball_detections)
        logger.debug(f"Kalman tracker returned {len(tracked_balls)} tracked balls")

        # --- Identification & State Estimation ---
        # First, identify all raw detections by color
        identified_detections = {}
        if raw_ball_detections and frame_image is not None:
            try:
                identified_detections = self.ball_identifier.identify_balls(raw_ball_detections, frame_image)
                logger.debug(f"Identified {len(identified_detections)} balls by color: {identified_detections}")
            except Exception as e:
                logger.error(f"Error identifying balls: {e}", exc_info=True)

        for ball in tracked_balls:
            track_id = ball['track_id']
            
            if track_id not in self.managed_balls:
                # This is a new track, we need to identify it
                logger.info(f"New track detected: {track_id}")
                
                # Find the closest raw detection to this tracked ball
                unique_id = None
                if raw_ball_detections:
                    tracked_pos = ball['smoothed_position_3d']
                    min_distance = float('inf')
                    closest_detection_idx = None
                    
                    for idx, detection in enumerate(raw_ball_detections):
                        det_pos = np.array([detection.position.x, detection.position.y, detection.position.z])
                        distance = np.linalg.norm(tracked_pos - det_pos)
                        if distance < min_distance:
                            min_distance = distance
                            closest_detection_idx = idx
                    
                    # Use the identified unique_id for the closest detection
                    if closest_detection_idx is not None and closest_detection_idx in identified_detections:
                        unique_id = identified_detections[closest_detection_idx]
                        logger.info(f"Track {track_id} matched to detection {closest_detection_idx} with unique_id {unique_id} (distance: {min_distance:.3f})")
                    else:
                        logger.warning(f"Track {track_id} could not be matched to an identified detection")
                
                # If we still don't have a unique_id, assign a temporary one
                if unique_id is None:
                    unique_id = f"unknown_{track_id}"
                    logger.warning(f"Track {track_id} assigned temporary unique_id: {unique_id}")
                
                self.managed_balls[track_id] = ManagedBall(track_id, unique_id)

            # Update the managed ball with data from the Kalman filter
            managed_ball = self.managed_balls[track_id]
            managed_ball.kf = self.kalman_tracker.tracks[track_id]
            managed_ball.smoothed_position_3d = ball['smoothed_position_3d']
            managed_ball.smoothed_velocity_3d = ball['smoothed_velocity_3d']
            managed_ball.position_history.append(ball['smoothed_position_3d'])
            
            logger.debug(f"Updated track {track_id}: pos={ball['smoothed_position_3d']}, vel={ball['smoothed_velocity_3d']}")

        # --- State Estimation ---
        try:
            self.state_estimator.estimate_state(self.managed_balls, hand_positions)
            logger.debug(f"State estimation completed for {len(self.managed_balls)} balls")
        except Exception as e:
            logger.error(f"Error in state estimation: {e}", exc_info=True)
        
        # --- Position Correction ---
        for ball in self.managed_balls.values():
            if ball.physical_state == PhysicalState.HELD_LEFT:
                ball.smoothed_position_3d = hand_positions.get('left')
            elif ball.physical_state == PhysicalState.HELD_RIGHT:
                ball.smoothed_position_3d = hand_positions.get('right')

        # TODO: Cleanup lost tracks

        return self.managed_balls

    def shutdown(self):
        """
        Saves any new or updated color profiles.
        """
        self.color_profile_manager.save_profiles()