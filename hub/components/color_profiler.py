import json
import uuid
import numpy as np
from pathlib import Path

class ColorProfile:
    """Stores the color profile for a single physical juggling ball."""

    def __init__(self, unique_id, profile_name, hsv_mean, hsv_range, sample_count=0):
        self.unique_id = unique_id
        self.profile_name = profile_name
        self.hsv_mean = np.array(hsv_mean, dtype=float)
        self.hsv_range = np.array(hsv_range, dtype=float)
        self.sample_count = sample_count

    def to_dict(self):
        """Serializes the object to a dictionary for JSON storage."""
        return {
            'unique_id': self.unique_id,
            'profile_name': self.profile_name,
            'hsv_mean': self.hsv_mean.tolist(),
            'hsv_range': self.hsv_range.tolist(),
            'sample_count': self.sample_count
        }

    @classmethod
    def from_dict(cls, data):
        """Creates a ColorProfile object from a dictionary."""
        return cls(
            unique_id=data['unique_id'],
            profile_name=data['profile_name'],
            hsv_mean=data['hsv_mean'],
            hsv_range=data['hsv_range'],
            sample_count=data['sample_count']
        )

class ColorProfileManager:
    """Manages loading, saving, and matching color profiles for juggling balls."""

    def __init__(self, profile_path='hub/ball_color_profiles.json'):
        """
        Initializes the manager.

        Args:
            profile_path (str or Path): The path to the JSON file where profiles are stored.
        """
        self.profile_path = Path(profile_path)
        self.profiles = {}  # Keyed by unique_id

    def load_profiles(self):
        """
        Loads color profiles from the JSON file. If the file doesn't exist,
        it does nothing.
        """
        if not self.profile_path.exists():
            print(f"Profile file not found at {self.profile_path}. A new one will be created on save.")
            return

        with open(self.profile_path, 'r') as f:
            try:
                profiles_data = json.load(f)
                for unique_id, profile_data in profiles_data.items():
                    # Support old and new profile id formats
                    profile_id = profile_data.get('unique_id', unique_id)
                    self.profiles[profile_id] = ColorProfile.from_dict(profile_data)
                print(f"Loaded {len(self.profiles)} color profiles.")
            except json.JSONDecodeError:
                print(f"Error decoding JSON from {self.profile_path}. Starting with empty profiles.")
                self.profiles = {}


    def save_profiles(self):
        """
        Saves the current color profiles to the JSON file.
        """
        self.profile_path.parent.mkdir(parents=True, exist_ok=True)
        profiles_to_save = {uid: profile.to_dict() for uid, profile in self.profiles.items()}
        with open(self.profile_path, 'w') as f:
            json.dump(profiles_to_save, f, indent=4)
        print(f"Saved {len(self.profiles)} color profiles to {self.profile_path}.")


    def match_color(self, hsv_color_sample):
        """
        Finds the best matching color profile for a given HSV color sample.

        Args:
            hsv_color_sample (np.ndarray): The HSV color to match.

        Returns:
            str or None: The unique_id of the best matching profile, or None if no match is found.
        """
        hsv_color_sample = np.array(hsv_color_sample, dtype=float)
        best_match_id = None
        smallest_distance = float('inf')

        for unique_id, profile in self.profiles.items():
            # Check if the sample is within the profile's range
            lower_bound = profile.hsv_mean - profile.hsv_range
            upper_bound = profile.hsv_mean + profile.hsv_range
            
            # Handle hue wrap-around for lower bound
            if lower_bound[0] < 0:
                hue_in_range = (hsv_color_sample[0] >= (180 + lower_bound[0])) or \
                               (hsv_color_sample[0] <= upper_bound[0])
            # Handle hue wrap-around for upper bound
            elif upper_bound[0] > 180:
                hue_in_range = (hsv_color_sample[0] >= lower_bound[0]) or \
                               (hsv_color_sample[0] <= (upper_bound[0] - 180))
            else:
                hue_in_range = lower_bound[0] <= hsv_color_sample[0] <= upper_bound[0]

            # Check saturation and value
            sv_in_range = np.all(lower_bound[1:] <= hsv_color_sample[1:]) and \
                          np.all(hsv_color_sample[1:] <= upper_bound[1:])

            if hue_in_range and sv_in_range:
                # If it's a match, calculate distance to find the *best* match
                # Using a simple Euclidean distance for now
                distance = np.linalg.norm(profile.hsv_mean - hsv_color_sample)
                if distance < smallest_distance:
                    smallest_distance = distance
                    best_match_id = unique_id
        
        return best_match_id

    def update_profile(self, unique_id, hsv_color_sample):
        """
        Updates a profile's mean and range with a new color sample.

        Args:
            unique_id (str): The ID of the profile to update.
            hsv_color_sample (np.ndarray): The new HSV color to add.
        """
        if unique_id not in self.profiles:
            return

        profile = self.profiles[unique_id]
        hsv_color_sample = np.array(hsv_color_sample, dtype=float)

        # Update running mean
        new_sample_count = profile.sample_count + 1
        profile.hsv_mean = (profile.hsv_mean * profile.sample_count + hsv_color_sample) / new_sample_count
        profile.sample_count = new_sample_count

        # Expand range if necessary
        # This is a simple approach; more sophisticated methods exist
        diff = np.abs(hsv_color_sample - profile.hsv_mean)
        profile.hsv_range = np.maximum(profile.hsv_range, diff * 1.2) # Add a small buffer

    def create_new_profile(self, hsv_color_sample, profile_name=None):
        """
        Creates a new color profile from a color sample.

        Args:
            hsv_color_sample (np.ndarray): The first color sample for the new profile.
            profile_name (str): The human-readable name for the profile. If None, generates a default name.

        Returns:
            str: The unique_id of the newly created profile.
        """
        new_id = str(uuid.uuid4())
        
        # Use provided name or generate a default one
        if profile_name is None:
            profile_name = f"Ball_{len(self.profiles) + 1}"
        
        initial_range = np.array([10, 50, 50], dtype=float) # Initial forgiving range

        new_profile = ColorProfile(
            unique_id=new_id,
            profile_name=profile_name,
            hsv_mean=hsv_color_sample,
            hsv_range=initial_range,
            sample_count=1
        )
        self.profiles[new_id] = new_profile
        print(f"Created new color profile '{profile_name}' with ID {new_id}")
        return new_id
    
    def get_profile_by_name(self, profile_name):
        """
        Gets a profile by its human-readable name.
        
        Args:
            profile_name (str): The name of the profile to find.
            
        Returns:
            ColorProfile or None: The profile if found, None otherwise.
        """
        for profile in self.profiles.values():
            if profile.profile_name == profile_name:
                return profile
        return None