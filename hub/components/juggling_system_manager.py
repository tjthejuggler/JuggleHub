from components.color_profiler import ColorProfileManager
from components.ball_identifier import BallIdentifier
from components.kalman_tracker import KalmanBallTracker
from components.state_estimator import ProbabilisticStateEstimator
from components.managed_ball import ManagedBall, PhysicalState

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

        # --- Kalman Update ---
        tracked_balls = self.kalman_tracker.update(raw_ball_detections)

        # --- Identification & State Estimation ---
        for ball in tracked_balls:
            track_id = ball['track_id']
            if track_id not in self.managed_balls:
                # This is a new track, identify it by color
                # Note: This is a simplified approach. A more robust implementation
                # would be needed to associate a raw detection with a track.
                unique_id = self.ball_identifier.identify_balls([raw_ball_detections[0]], frame_image)[0]
                self.managed_balls[track_id] = ManagedBall(track_id, unique_id)

            # Update the managed ball with data from the Kalman filter
            managed_ball = self.managed_balls[track_id]
            managed_ball.kf = self.kalman_tracker.tracks[track_id]
            managed_ball.smoothed_position_3d = ball['smoothed_position_3d']
            managed_ball.smoothed_velocity_3d = ball['smoothed_velocity_3d']
            managed_ball.position_history.append(ball['smoothed_position_3d'])

        # --- State Estimation ---
        self.state_estimator.estimate_state(self.managed_balls, hand_positions)
        
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