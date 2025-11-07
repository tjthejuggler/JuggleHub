"""
Time-Based Color Calibration for Depth Blobs

This module implements a multi-frame color calibration system that collects
color samples from depth blobs over a 10-second period while the user juggles.
"""

import time
import json
import os
import numpy as np
from enum import Enum
from typing import List, Tuple, Optional
from PyQt6.QtCore import QTimer, QObject, pyqtSignal
from PyQt6.QtWidgets import QLabel


class CalibrationState(Enum):
    """Calibration state machine states"""
    IDLE = "idle"
    PREPARATION = "preparation"
    RECORDING = "recording"
    PROCESSING = "processing"
    COMPLETE = "complete"
    ERROR = "error"


class TimeBasedCalibration(QObject):
    """
    Manages time-based color calibration workflow.
    
    Workflow:
    1. IDLE → User clicks calibrate button
    2. PREPARATION (5s) → Countdown, user gets ready
    3. RECORDING (10s) → Collect color samples from depth blobs
    4. PROCESSING → Calculate average hue/saturation
    5. COMPLETE → Save to JSON and update UI
    """
    
    # Signals for UI updates
    state_changed = pyqtSignal(str, str)  # (state, message)
    countdown_update = pyqtSignal(int)  # seconds remaining
    calibration_complete = pyqtSignal(str, float, float, int)  # (color_name, avg_hue, avg_sat, sample_count)
    calibration_error = pyqtSignal(str)  # error message
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.state = CalibrationState.IDLE
        self.color_name = None
        self.samples = []  # List of (hue, saturation) tuples
        
        # Timers
        self.prep_timer = QTimer(self)
        self.prep_timer.timeout.connect(self._on_prep_tick)
        self.prep_countdown = 0
        
        self.record_timer = QTimer(self)
        self.record_timer.timeout.connect(self._on_record_tick)
        self.record_countdown = 0
        
        # Configuration
        self.PREP_DURATION = 5  # seconds
        self.RECORD_DURATION = 10  # seconds
        self.MIN_SAMPLES = 50  # Minimum samples required for valid calibration
        
        # Settings file path
        self.settings_path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "..",
            "calibration_settings_new3d.json"
        )
        self.settings_path = os.path.normpath(self.settings_path)
    
    def start_calibration(self, color_name: str) -> bool:
        """
        Start calibration for specified color.
        
        Args:
            color_name: Name of color to calibrate (e.g., "pink", "red")
            
        Returns:
            True if calibration started successfully, False otherwise
        """
        if self.state != CalibrationState.IDLE:
            self.calibration_error.emit(f"Calibration already in progress (state: {self.state.value})")
            return False
        
        self.color_name = color_name
        self.samples = []
        
        # Start preparation phase
        self._set_state(CalibrationState.PREPARATION)
        self.prep_countdown = self.PREP_DURATION
        self.state_changed.emit(
            self.state.value,
            f"Get ready to juggle the {color_name} ball! Starting in {self.prep_countdown} seconds..."
        )
        self.countdown_update.emit(self.prep_countdown)
        self.prep_timer.start(1000)  # 1 second intervals
        
        print(f"🎨 Started calibration for '{color_name}' - Preparation phase ({self.PREP_DURATION}s)")
        return True
    
    def cancel_calibration(self):
        """Cancel ongoing calibration"""
        self.prep_timer.stop()
        self.record_timer.stop()
        self.samples = []
        self._set_state(CalibrationState.IDLE)
        self.state_changed.emit(self.state.value, "Calibration cancelled")
        print("❌ Calibration cancelled by user")
    
    def add_color_sample(self, hue: float, saturation: float):
        """
        Add a color sample during recording phase.
        
        Args:
            hue: Hue value (0-180 in OpenCV HSV)
            saturation: Saturation value (0-255)
        """
        if self.state != CalibrationState.RECORDING:
            print(f"[Calibration] ✗ Rejected sample (not recording): H={hue:.1f}° S={saturation:.1f}, state={self.state.value}")
            return
        
        # Validate values
        if 0 <= hue <= 180 and 0 <= saturation <= 255:
            self.samples.append((hue, saturation))
            print(f"[Calibration] ✓ Added sample #{len(self.samples)}: H={hue:.1f}° S={saturation:.1f}")
        else:
            print(f"[Calibration] ✗ Invalid sample values: H={hue:.1f}° (valid: 0-180), S={saturation:.1f} (valid: 0-255)")
    
    def _on_prep_tick(self):
        """Handle preparation timer tick"""
        self.prep_countdown -= 1
        
        if self.prep_countdown > 0:
            self.state_changed.emit(
                self.state.value,
                f"Get ready to juggle! Starting in {self.prep_countdown}..."
            )
            self.countdown_update.emit(self.prep_countdown)
        else:
            # Preparation complete, start recording
            self.prep_timer.stop()
            self._start_recording()
    
    def _start_recording(self):
        """Start the recording phase"""
        self._set_state(CalibrationState.RECORDING)
        self.record_countdown = self.RECORD_DURATION
        self.samples = []  # Clear any existing samples
        
        self.state_changed.emit(
            self.state.value,
            f"Juggle the {self.color_name} ball! Recording: {self.record_countdown} seconds..."
        )
        self.countdown_update.emit(self.record_countdown)
        self.record_timer.start(1000)  # 1 second intervals
        
        print(f"🎬 Recording phase started - collecting samples for {self.RECORD_DURATION}s")
    
    def _on_record_tick(self):
        """Handle recording timer tick"""
        self.record_countdown -= 1
        
        if self.record_countdown > 0:
            self.state_changed.emit(
                self.state.value,
                f"Juggle the {self.color_name} ball! Recording: {self.record_countdown} seconds..."
            )
            self.countdown_update.emit(self.record_countdown)
        else:
            # Recording complete, process samples
            self.record_timer.stop()
            self._process_samples()
    
    def _process_samples(self):
        """Process collected samples and save results"""
        self._set_state(CalibrationState.PROCESSING)
        self.state_changed.emit(self.state.value, "Processing samples...")
        
        print(f"📊 Processing {len(self.samples)} color samples...")
        
        # Check if we have enough samples
        if len(self.samples) < self.MIN_SAMPLES:
            error_msg = f"Not enough samples collected ({len(self.samples)} < {self.MIN_SAMPLES}). Please try again."
            self.calibration_error.emit(error_msg)
            self._set_state(CalibrationState.ERROR)
            print(f"❌ {error_msg}")
            
            # Reset to idle after 3 seconds
            QTimer.singleShot(3000, lambda: self._set_state(CalibrationState.IDLE))
            return
        
        # Calculate median hue and saturation (more robust than mean)
        hues = [s[0] for s in self.samples]
        saturations = [s[1] for s in self.samples]
        
        avg_hue = float(np.median(hues))
        avg_saturation = float(np.median(saturations))
        
        print(f"✅ Calculated median values: H={avg_hue:.1f}° S={avg_saturation:.1f}")
        print(f"   Sample statistics: H range=[{min(hues):.1f}, {max(hues):.1f}], S range=[{min(saturations):.1f}, {max(saturations):.1f}]")
        
        # Save to JSON
        if self._save_calibration(avg_hue, avg_saturation):
            self._set_state(CalibrationState.COMPLETE)
            self.calibration_complete.emit(self.color_name, avg_hue, avg_saturation, len(self.samples))
            self.state_changed.emit(
                self.state.value,
                f"✅ Calibration complete! Captured {len(self.samples)} samples."
            )
            print(f"🎉 Calibration complete for '{self.color_name}'")
            
            # Reset to idle after 3 seconds
            QTimer.singleShot(3000, lambda: self._set_state(CalibrationState.IDLE))
        else:
            error_msg = "Failed to save calibration data"
            self.calibration_error.emit(error_msg)
            self._set_state(CalibrationState.ERROR)
            print(f"❌ {error_msg}")
            
            # Reset to idle after 3 seconds
            QTimer.singleShot(3000, lambda: self._set_state(CalibrationState.IDLE))
    
    def _save_calibration(self, avg_hue: float, avg_saturation: float) -> bool:
        """
        Save calibration results to JSON file.
        
        Args:
            avg_hue: Average hue value
            avg_saturation: Average saturation value
            
        Returns:
            True if save successful, False otherwise
        """
        try:
            # Load existing settings
            with open(self.settings_path, 'r') as f:
                settings = json.load(f)
            
            # Find and update the color profile
            color_profiles = settings.get('color_profiles', [])
            profile_found = False
            
            for profile in color_profiles:
                if profile['name'] == self.color_name:
                    profile['avg_hue'] = avg_hue
                    profile['avg_saturation'] = avg_saturation
                    profile_found = True
                    print(f"✅ Updated profile for '{self.color_name}': H={avg_hue:.1f}° S={avg_saturation:.1f}")
                    break
            
            if not profile_found:
                print(f"⚠️ Warning: Color profile '{self.color_name}' not found in settings")
                return False
            
            # Save back to file
            with open(self.settings_path, 'w') as f:
                json.dump(settings, f, indent=4)
            
            print(f"💾 Saved calibration to {self.settings_path}")
            return True
            
        except Exception as e:
            print(f"❌ Error saving calibration: {e}")
            import traceback
            traceback.print_exc()
            return False
    
    def _set_state(self, new_state: CalibrationState):
        """Update calibration state"""
        old_state = self.state
        self.state = new_state
        print(f"🔄 Calibration state: {old_state.value} → {new_state.value}")
    
    def is_recording(self) -> bool:
        """Check if currently in recording phase"""
        return self.state == CalibrationState.RECORDING
    
    def is_active(self) -> bool:
        """Check if calibration is active (not idle)"""
        return self.state != CalibrationState.IDLE