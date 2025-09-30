from enum import Enum

class DetectionState(Enum):
    DETECTED = 1
    UNDETECTED = 2

class PhysicalState(Enum):
    UNHELD = 1
    HELD_LEFT = 2
    HELD_RIGHT = 3

class ManagedBall:
    def __init__(self, track_id, unique_id):
        # --- Identity ---
        self.track_id = track_id   # The temporary ID from the current tracking session (e.g., from ByteTrack)
        self.unique_id = unique_id # The persistent, color-based ID from ColorProfileManager
        self.logical_id = None     # The user-assigned logical ID (0, 1, 2, etc.) - set after calibration

        # --- State ---
        self.detection_state = DetectionState.DETECTED
        self.physical_state = PhysicalState.UNHELD
        self.confidences = {'held_left': 0.0, 'held_right': 0.0, 'unheld': 1.0}

        # --- Physics & Position ---
        self.kf = None # Placeholder for the Kalman Filter object
        self.position_history = [] # List of 3D points
        self.smoothed_position_3d = None
        self.smoothed_velocity_3d = None
        self.projected_pos_2d = None  # 2D position for display (from detection bounding box center)
        self.bounding_box_2d = None   # Bounding box for color sampling

        # --- Hysteresis Logic ---
        self.potential_next_state = self.physical_state
        self.frames_in_potential_state = 0