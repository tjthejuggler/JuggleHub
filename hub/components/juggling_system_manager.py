import logging
import numpy as np
from components.color_profiler import ColorProfileManager
from components.ball_identifier import BallIdentifier
from components.state_estimator import ProbabilisticStateEstimator
from components.managed_ball import ManagedBall, PhysicalState, DetectionState

logger = logging.getLogger(__name__)

class JugglingSystemManager:
    """
    Orchestrates all components of the juggling tracking system.
    
    NEW APPROACH: Color profiles ARE the identity. We track balls directly by their
    color in every frame, with instant position updates (no Kalman smoothing lag).
    """

    def __init__(self, config):
        self.color_profile_manager = ColorProfileManager()
        self.color_profile_manager.load_profiles()
        self.ball_identifier = BallIdentifier(self.color_profile_manager)
        self.state_estimator = ProbabilisticStateEstimator(config)
        
        # Managed balls keyed by color profile unique_id (NOT track_id)
        # This ensures consistent identity across frames
        self.managed_balls = {}
        
        # Mapping from logical ball ID (0, 1, 2, etc.) to color profile unique_id
        self.logical_id_to_profile = {}
        
        # Track how many frames each ball has been missing
        self.frames_missing = {}
        self.max_missing_frames = 30  # Drop ball after 1 second at 30fps
    
    def assign_profile_to_logical_id(self, logical_id, profile_unique_id):
        """
        Associates a logical ball ID (e.g., 0, 1, 2) with a color profile.
        
        Args:
            logical_id (int): The logical ball ID (0, 1, 2, etc.)
            profile_unique_id (str): The unique ID of the color profile
        """
        self.logical_id_to_profile[logical_id] = profile_unique_id
        logger.debug(f"Assigned logical ball {logical_id} to color profile {profile_unique_id}")
        
        # Create managed ball immediately if it doesn't exist
        if profile_unique_id not in self.managed_balls:
            self.managed_balls[profile_unique_id] = ManagedBall(profile_unique_id, profile_unique_id)
            self.managed_balls[profile_unique_id].logical_id = logical_id
            self.frames_missing[profile_unique_id] = 0
            logger.debug(f"Created managed ball for profile {profile_unique_id} with logical_id {logical_id}")

    def process_frame(self, frame_data, frame_image):
        """
        Processes a single frame of data from the C++ engine.
        
        NEW APPROACH: Direct color-based tracking with INSTANT position updates.
        No Kalman smoothing, no ByteTrack IDs - just pure color identity.

        Args:
            frame_data (FrameData): The protobuf message from the engine.
            frame_image (np.ndarray): The BGR image frame.

        Returns:
            dict: A dictionary of fully updated ManagedBall objects keyed by profile unique_id.
        """
        raw_ball_detections = frame_data.balls
        hand_positions = {hand.side: np.array([hand.position_3d.x, hand.position_3d.y, hand.position_3d.z])
                         for hand in frame_data.hands}

        logger.debug(f"Processing frame with {len(raw_ball_detections)} raw detections")

        # --- Step 1: Identify all detections by color ---
        identified_detections = {}
        if raw_ball_detections and frame_image is not None:
            try:
                identified_detections = self.ball_identifier.identify_balls(raw_ball_detections, frame_image)
                logger.debug(f"Identified {len(identified_detections)} balls by color: {identified_detections}")
            except Exception as e:
                logger.error(f"Error identifying balls: {e}", exc_info=True)

        # --- Step 2: Mark all balls as not seen this frame ---
        balls_seen_this_frame = set()

        # --- Step 3: Update positions for all identified balls ---
        for detection_idx, profile_unique_id in identified_detections.items():
            detection = raw_ball_detections[detection_idx]
            
            # Create managed ball if it doesn't exist
            if profile_unique_id not in self.managed_balls:
                # Find logical ID for this profile
                logical_id = None
                for lid, pid in self.logical_id_to_profile.items():
                    if pid == profile_unique_id:
                        logical_id = lid
                        break
                
                self.managed_balls[profile_unique_id] = ManagedBall(profile_unique_id, profile_unique_id)
                self.managed_balls[profile_unique_id].logical_id = logical_id
                self.frames_missing[profile_unique_id] = 0
                logger.debug(f"Created managed ball for profile {profile_unique_id} with logical_id {logical_id}")
            
            # INSTANT POSITION UPDATE - No smoothing, no lag!
            managed_ball = self.managed_balls[profile_unique_id]
            managed_ball.smoothed_position_3d = np.array([
                detection.position.x,
                detection.position.y,
                detection.position.z
            ])
            managed_ball.detection_state = DetectionState.DETECTED
            managed_ball.position_history.append(managed_ball.smoothed_position_3d)
            
            # Store 2D position and bounding box for display
            bbox = detection.bounding_box_2d
            managed_ball.projected_pos_2d = (
                bbox.x + bbox.width / 2,
                bbox.y + bbox.height / 2
            )
            managed_ball.bounding_box_2d = bbox
            
            # Calculate velocity from position history
            if len(managed_ball.position_history) >= 2:
                dt = 1.0 / 30.0  # Assume 30 FPS
                managed_ball.smoothed_velocity_3d = (
                    managed_ball.position_history[-1] - managed_ball.position_history[-2]
                ) / dt
            else:
                managed_ball.smoothed_velocity_3d = np.array([0.0, 0.0, 0.0])
            
            # Limit position history length
            if len(managed_ball.position_history) > 100:
                managed_ball.position_history.pop(0)
            
            # Reset missing frames counter
            self.frames_missing[profile_unique_id] = 0
            balls_seen_this_frame.add(profile_unique_id)
            
            logger.debug(f"Ball {profile_unique_id} (logical_id={managed_ball.logical_id}): "
                        f"INSTANT position update to {managed_ball.smoothed_position_3d}, 2D pos: {managed_ball.projected_pos_2d}")

        # --- Step 4: Handle balls not seen this frame ---
        for profile_unique_id in list(self.managed_balls.keys()):
            if profile_unique_id not in balls_seen_this_frame:
                self.frames_missing[profile_unique_id] += 1
                managed_ball = self.managed_balls[profile_unique_id]
                managed_ball.detection_state = DetectionState.UNDETECTED
                
                logger.debug(f"Ball {profile_unique_id} not seen for {self.frames_missing[profile_unique_id]} frames")
                
                # Remove ball if missing too long
                if self.frames_missing[profile_unique_id] > self.max_missing_frames:
                    logger.debug(f"Removing ball {profile_unique_id} - missing for {self.frames_missing[profile_unique_id]} frames")
                    del self.managed_balls[profile_unique_id]
                    del self.frames_missing[profile_unique_id]

        # --- Step 5: State Estimation ---
        try:
            self.state_estimator.estimate_state(self.managed_balls, hand_positions)
            logger.debug(f"State estimation completed for {len(self.managed_balls)} balls")
        except Exception as e:
            logger.error(f"Error in state estimation: {e}", exc_info=True)
        
        # --- Step 6: Position Correction for Held Balls ---
        for ball in self.managed_balls.values():
            # If ball is held and not detected, snap it to the hand position
            if ball.detection_state == DetectionState.UNDETECTED:
                if ball.physical_state == PhysicalState.HELD_LEFT and 'left' in hand_positions:
                    ball.smoothed_position_3d = hand_positions['left']
                    logger.debug(f"Ball {ball.unique_id}: Snapped to left hand (held + undetected)")
                elif ball.physical_state == PhysicalState.HELD_RIGHT and 'right' in hand_positions:
                    ball.smoothed_position_3d = hand_positions['right']
                    logger.debug(f"Ball {ball.unique_id}: Snapped to right hand (held + undetected)")

        return self.managed_balls

    def shutdown(self):
        """
        Saves any new or updated color profiles.
        """
        self.color_profile_manager.save_profiles()