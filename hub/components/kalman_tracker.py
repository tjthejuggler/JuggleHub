import numpy as np
from filterpy.kalman import KalmanFilter
from scipy.optimize import linear_sum_assignment

class KalmanBallTracker:
    """
    Manages ball tracking using Kalman Filters for smoothing and prediction.
    """

    def __init__(self, dt=1/30.0, gravity=-9.81):
        self.dt = dt
        self.gravity = gravity
        self.tracks = {}  # key: track_id, value: KalmanFilter instance
        self.next_track_id = 0

    def _create_kalman_filter(self, initial_pos):
        """Creates a new Kalman Filter for a ball."""
        kf = KalmanFilter(dim_x=6, dim_z=3)
        
        # State transition matrix
        kf.F = np.array([
            [1, 0, 0, self.dt, 0, 0],
            [0, 1, 0, 0, self.dt, 0],
            [0, 0, 1, 0, 0, self.dt],
            [0, 0, 0, 1, 0, 0],
            [0, 0, 0, 0, 1, 0],
            [0, 0, 0, 0, 0, 1]
        ])
        
        # Measurement function
        kf.H = np.array([
            [1, 0, 0, 0, 0, 0],
            [0, 1, 0, 0, 0, 0],
            [0, 0, 1, 0, 0, 0]
        ])
        
        # Initial state
        kf.x = np.array([initial_pos[0], initial_pos[1], initial_pos[2], 0, 0, 0]).T
        
        # Process noise
        kf.Q *= 0.1
        
        # Measurement noise
        kf.R *= 5
        
        return kf

    def update(self, raw_ball_detections):
        """
        Updates the tracker with new ball detections.

        Args:
            raw_ball_detections (list): A list of Ball protobuf objects.

        Returns:
            list: A list of objects with tracking information.
        """
        # Extract 3D positions from protobuf objects
        detected_positions = [np.array([b.position.x, b.position.y, b.position.z]) for b in raw_ball_detections]

        if not self.tracks:
            # Create new tracks for all initial detections
            for pos in detected_positions:
                self.tracks[self.next_track_id] = self._create_kalman_filter(pos)
                self.next_track_id += 1
            return []

        # Predict next state for all tracks
        for track_id, kf in self.tracks.items():
            kf.predict()
            # Apply gravity
            kf.x[5] += self.gravity * self.dt

        # Data association
        cost_matrix = np.zeros((len(self.tracks), len(detected_positions)))
        track_ids = list(self.tracks.keys())
        
        for i, track_id in enumerate(track_ids):
            for j, pos in enumerate(detected_positions):
                predicted_pos = self.tracks[track_id].x[:3]
                cost_matrix[i, j] = np.linalg.norm(predicted_pos - pos)

        row_ind, col_ind = linear_sum_assignment(cost_matrix)

        # Update matched tracks
        for r, c in zip(row_ind, col_ind):
            track_id = track_ids[r]
            self.tracks[track_id].update(detected_positions[c])

        # TODO: Handle new and lost tracks

        # Output results
        results = []
        for track_id, kf in self.tracks.items():
            pos = kf.x[:3]
            vel = kf.x[3:]
            # A simple innovation score
            innovation_score = np.linalg.norm(kf.y)
            
            results.append({
                'track_id': track_id,
                'smoothed_position_3d': pos,
                'smoothed_velocity_3d': vel,
                'innovation_score': innovation_score,
                'detection_state': 'DETECTED' # This will be improved later
            })
            
        return results