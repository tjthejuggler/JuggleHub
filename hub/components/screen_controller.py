#!/usr/bin/env python3
"""
JuggleHub - Screen Controller Component
"""

import subprocess

class ScreenController:
    """
    A class to control the laptop screens using kscreen-doctor.
    It maintains the state of both screens and applies changes with a single command.
    """

    def __init__(self):
        self._kscreen_doctor_path = "/usr/bin/kscreen-doctor"
        self._top_screen_enabled = True
        self._bottom_screen_enabled = True
        print("ScreenController initialized.")

    def _apply_screen_state(self):
        """
        Applies the current screen state by running a single kscreen-doctor command.
        """
        top_state = "enable" if self._top_screen_enabled else "disable"
        bottom_state = "enable" if self._bottom_screen_enabled else "disable"

        command = [
            self._kscreen_doctor_path,
            f"output.eDP-1.{top_state}",
            f"output.eDP-2.{bottom_state}"
        ]

        try:
            print(f"Running command: {' '.join(command)}")
            subprocess.run(command, check=True, capture_output=True, text=True, timeout=2)
            print(f"Successfully set screen states: Top={top_state}, Bottom={bottom_state}")
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError) as e:
            print(f"Error executing kscreen-doctor command: {getattr(e, 'stderr', e)}")

    # --- Top Screen (eDP-1) Controls ---
    def enable_top_screen(self):
        self._top_screen_enabled = True
        self._apply_screen_state()

    def disable_top_screen(self):
        self._top_screen_enabled = False
        self._apply_screen_state()

    # --- Bottom Screen (eDP-2) Controls ---
    def enable_bottom_screen(self):
        self._bottom_screen_enabled = True
        self._apply_screen_state()

    def disable_bottom_screen(self):
        self._bottom_screen_enabled = False
        self._apply_screen_state()