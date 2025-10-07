"""
UI Component

Provides a simple UI for visualizing juggling data from the engine.
"""

import sys
import time
import threading
from typing import Optional, Dict, Any
import os
import base64
import socket
import qrcode
import io
import cv2
import numpy as np
import json
from datetime import datetime
import subprocess
import platform

# Import extracted components
from .ui_network import UdpClient
from .ui_console import ConsoleUI
from .color_profile_manager import ColorProfileManager, ColorProfileDialog

# Ball management components removed - using legacy color tracking only
BALL_MANAGEMENT_AVAILABLE = False


try:
    import juggler_pb2
except ImportError:
    print("❌ Error: Protocol Buffer files not found. Please run 'make generate-proto' first.")
    sys.exit(1)

# Try to import PyQt6, fall back to console UI if not available
try:
    from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                                 QHBoxLayout, QLabel, QTextEdit, QPushButton,
                                 QGroupBox, QGridLayout, QProgressBar, QGraphicsView,
                                 QGraphicsScene, QGraphicsPixmapItem, QSlider, QLineEdit,
                                 QComboBox, QMessageBox, QDialog, QVBoxLayout as QVBoxLayout_Dialog,
                                 QMenuBar, QFileDialog, QScrollArea, QTabWidget)
    from PyQt6.QtCore import QTimer, pyqtSignal, QObject, Qt
    from PyQt6.QtGui import QFont, QPalette, QColor, QPixmap, QImage, QPen, QPainter, QKeySequence, QBrush, QAction
    PYQT_AVAILABLE = True
except ImportError:
    print("⚠️ PyQt6 not available. Using console UI.")
    PYQT_AVAILABLE = False

# Import PyQt-dependent components
if PYQT_AVAILABLE:
    from .ui_widgets import FrameDataSignal, CollapsibleGroupBox
    from .ui_settings import CalibrationSettingsWidget


if PYQT_AVAILABLE:
    # Note: CollapsibleGroupBox and FrameDataSignal are now imported from ui_widgets
    # Note: CalibrationSettingsWidget is now imported from ui_settings

    class CollapsibleGroupBox(QWidget):
        """
        A collapsible group box that mimics QGroupBox appearance.
        
        Features:
        - Clickable header with expand/collapse icon
        - Maintains QGroupBox styling
        - Remembers collapsed state in settings
        """
        
        def __init__(self, title: str, parent=None, collapsed: bool = False):
            super().__init__(parent)
            self.title = title
            self.is_collapsed = collapsed
            
            # Main layout
            main_layout = QVBoxLayout(self)
            main_layout.setContentsMargins(0, 0, 0, 5)
            main_layout.setSpacing(0)
            
            # Header button (clickable title)
            self.header_button = QPushButton(f"▼ {title}")
            self.header_button.setCheckable(True)
            self.header_button.setChecked(not collapsed)
            self.header_button.clicked.connect(self.toggle_collapsed)
            self.header_button.setStyleSheet("""
                QPushButton {
                    text-align: left;
                    padding: 8px;
                    border: 2px solid #555555;
                    border-radius: 5px 5px 0 0;
                    background-color: #3a3a3a;
                    font-weight: bold;
                }
                QPushButton:hover {
                    background-color: #4a4a4a;
                }
            """)
            main_layout.addWidget(self.header_button)
            
            # Content container
            self.content_widget = QWidget()
            self.content_widget.setObjectName("CollapsibleContent")
            self.content_layout = QVBoxLayout(self.content_widget)
            self.content_layout.setContentsMargins(10, 10, 10, 10)
            self.content_widget.setStyleSheet("""
                QWidget#CollapsibleContent {
                    border: 2px solid #555555;
                    border-top: none;
                    border-radius: 0 0 5px 5px;
                    background-color: #2b2b2b;
                }
            """)
            main_layout.addWidget(self.content_widget)
            
            # Set initial state
            if collapsed:
                self.content_widget.hide()
                self.header_button.setText(f"▶ {title}")
        
        def toggle_collapsed(self):
            self.is_collapsed = not self.is_collapsed
            if self.is_collapsed:
                self.content_widget.hide()
                self.header_button.setText(f"▶ {self.title}")
            else:
                self.content_widget.show()
                self.header_button.setText(f"▼ {self.title}")
        
        def get_content_layout(self):
            """Returns the layout where child widgets should be added"""
            return self.content_layout


    class JuggleHubMainWindow(QMainWindow):
        """Main window for JuggleHub UI."""
        
        def __init__(self, config: dict, zmq_client: 'ZMQClient', hub_instance=None):
            super().__init__()
            self.config = config
            self.zmq_client = zmq_client
            self.hub_instance = hub_instance
            self.frame_count = 0
            self.start_time = time.time()
            self.last_frame_data: Optional[juggler_pb2.FrameData] = None
            self.calibration_mode = False
            self.udp_client = UdpClient()
            self.is_continuous_recording = False
            self.tracker_history = {} # For drawing tails
            self.calibrating_id = -1 # ID of the ball we are currently calibrating
            self.color_profile_manager = ColorProfileManager()
            
            # Ball management removed - using legacy color tracking only
            
            # Signal for thread-safe updates
            self.signal_emitter = FrameDataSignal()
            self.signal_emitter.frame_received.connect(self._update_ui)
            
            self.init_ui()
            
            # Timer for periodic UI updates
            self.update_timer = QTimer()
            self.update_timer.timeout.connect(self._periodic_update)
            self.update_timer.start(100)  # Update every 100ms
        
        def init_ui(self):
            """Initialize the user interface."""
            self.setWindowTitle("JuggleHub - Juggling Analysis")
            self.setGeometry(100, 100, 1000, 700)
            # Maximize window on startup
            self.showMaximized()
            
            # Create menu bar
            self.create_menu_bar()
            
            # Central widget
            central_widget = QWidget()
            self.setCentralWidget(central_widget)
            
            # Main layout
            main_layout = QVBoxLayout(central_widget)
            
            # Status bar
            status_layout = QHBoxLayout()
            self.status_label = QLabel("🔄 Waiting for data...")
            self.fps_label = QLabel("FPS: 0.0")
            self.frame_count_label = QLabel("Frames: 0")
            
            status_layout.addWidget(self.status_label)
            status_layout.addStretch()
            status_layout.addWidget(self.fps_label)
            status_layout.addWidget(self.frame_count_label)
            
            main_layout.addLayout(status_layout)
            
            # Content area
            content_layout = QHBoxLayout()
            
            # Left panel - Ball tracking (will extend down to activity log)
            left_panel_layout = QVBoxLayout()
            
            ball_group = QGroupBox("🏀 Ball Tracking")
            ball_layout = QVBoxLayout(ball_group)
            
            self.ball_count_label = QLabel("Balls detected: 0")
            ball_layout.addWidget(self.ball_count_label)
            
            self.ball_list = QTextEdit()
            self.ball_list.setReadOnly(True)
            ball_layout.addWidget(self.ball_list)
            
            # Calibration mode is now always on - no button needed

            # Record button
            self.record_button = QPushButton("Record 5s Clip")
            self.record_button.clicked.connect(self.record_clip)
            ball_layout.addWidget(self.record_button)

            # Continuous recording button
            self.continuous_record_button = QPushButton("Start Recording")
            self.continuous_record_button.clicked.connect(self.toggle_continuous_recording)
            self.continuous_record_button.setCheckable(True)
            self.continuous_record_button.setStyleSheet("""
                QPushButton {
                    background-color: #4CAF50;
                    color: white;
                    border: none;
                    padding: 8px;
                    border-radius: 4px;
                    font-weight: bold;
                }
                QPushButton:checked {
                    background-color: #f44336;
                }
                QPushButton:hover {
                    background-color: #45a049;
                }
                QPushButton:checked:hover {
                    background-color: #da190b;
                }
            """)
            ball_layout.addWidget(self.continuous_record_button)
            
            # Recording status indicator
            self.recording_status = QLabel("● Not Recording")
            self.recording_status.setStyleSheet("color: #666666; font-weight: bold;")
            ball_layout.addWidget(self.recording_status)

            left_panel_layout.addWidget(ball_group)

            # Center panel - Video Feed and Calibration Visualization
            center_panel_layout = QVBoxLayout()
            
            self.video_group = QGroupBox("📹 Camera Feed")
            self.video_layout = QVBoxLayout(self.video_group)
            self.video_scene = QGraphicsScene()
            self.video_view = QGraphicsView(self.video_scene)
            
            # Configure video view to minimize extra space
            self.video_view.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            self.video_view.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            self.video_view.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self.video_view.setMaximumHeight(400)  # Limit height to about 2/3
            
            self.video_pixmap_item = QGraphicsPixmapItem()
            self.video_scene.addItem(self.video_pixmap_item)
            self.video_view.mousePressEvent = self.video_view_clicked
            self.video_layout.addWidget(self.video_view)
            
            center_panel_layout.addWidget(self.video_group)
            
            # Set calibration mode to always on
            self.calibration_mode = True
            
            # Right panel - Calibration Visualization (extends to right of Ball Tracking)
            right_panel_layout = QVBoxLayout()
            
            calibration_group = QGroupBox("🎨 Calibration & Visualization")
            calibration_layout = QVBoxLayout(calibration_group)
            
            # --- Color Profile Controls ---
            color_profile_layout = QHBoxLayout()
            color_profile_layout.addWidget(QLabel("Color Profile:"))
            
            self.color_profile_combo = QComboBox()
            self.populate_color_profiles()
            color_profile_layout.addWidget(self.color_profile_combo)
            
            self.set_color_profile_button = QPushButton("Set Color Profile")
            self.set_color_profile_button.setCheckable(True)
            self.set_color_profile_button.clicked.connect(self.start_color_profile_calibration)
            self.set_color_profile_button.setStyleSheet("""
                QPushButton {
                    background-color: #555;
                    color: white;
                    border: 1px solid #777;
                    padding: 5px;
                    border-radius: 3px;
                }
                QPushButton:checked {
                    background-color: #007ACC;
                    border-color: #005A9E;
                }
            """)
            color_profile_layout.addWidget(self.set_color_profile_button)
            calibration_layout.addLayout(color_profile_layout)
            
            # Status label
            self.color_profile_status_label = QLabel("Select a color profile and click 'Set Color Profile', then click on a ball in the video.")
            self.color_profile_status_label.setWordWrap(True)
            calibration_layout.addWidget(self.color_profile_status_label)
            
            # --- Visualization Toggles (Pipeline Steps) ---
            # Row 1: Early pipeline steps
            toggles_row1 = QHBoxLayout()
            toggles_row1.setSpacing(5)
            
            self.show_kalman_predictions_toggle = QPushButton("2. Kalman Predictions")
            self.show_kalman_predictions_toggle.setCheckable(True)
            self.show_kalman_predictions_toggle.setChecked(False)
            self.show_kalman_predictions_toggle.clicked.connect(self.toggle_overlays)
            self.show_kalman_predictions_toggle.setMaximumWidth(170)
            self.show_kalman_predictions_toggle.setToolTip("Show predicted positions from Kalman filters (blue circles)")
            toggles_row1.addWidget(self.show_kalman_predictions_toggle)
            
            self.show_raw_detections_toggle = QPushButton("3. YOLO Detections")
            self.show_raw_detections_toggle.setCheckable(True)
            self.show_raw_detections_toggle.setChecked(False)
            self.show_raw_detections_toggle.clicked.connect(self.toggle_overlays)
            self.show_raw_detections_toggle.setMaximumWidth(160)
            self.show_raw_detections_toggle.setToolTip("Show raw YOLO detections (red boxes)")
            toggles_row1.addWidget(self.show_raw_detections_toggle)
            
            self.show_filtered_detections_toggle = QPushButton("4. Filtered Detections")
            self.show_filtered_detections_toggle.setCheckable(True)
            self.show_filtered_detections_toggle.setChecked(False)
            self.show_filtered_detections_toggle.clicked.connect(self.toggle_overlays)
            self.show_filtered_detections_toggle.setMaximumWidth(170)
            self.show_filtered_detections_toggle.setToolTip("Show rejected detections (gray crossed out)")
            toggles_row1.addWidget(self.show_filtered_detections_toggle)
            
            toggles_row1.addStretch()
            calibration_layout.addLayout(toggles_row1)
            
            # Row 2: Matching and initialization
            toggles_row2 = QHBoxLayout()
            toggles_row2.setSpacing(5)
            
            self.show_associations_toggle = QPushButton("5. 3D Matching")
            self.show_associations_toggle.setCheckable(True)
            self.show_associations_toggle.setChecked(False)
            self.show_associations_toggle.clicked.connect(self.toggle_overlays)
            self.show_associations_toggle.setMaximumWidth(140)
            self.show_associations_toggle.setToolTip("Show tracker-detection associations (green lines)")
            toggles_row2.addWidget(self.show_associations_toggle)
            
            self.show_new_trackers_toggle = QPushButton("6. Auto-Init")
            self.show_new_trackers_toggle.setCheckable(True)
            self.show_new_trackers_toggle.setChecked(False)
            self.show_new_trackers_toggle.clicked.connect(self.toggle_overlays)
            self.show_new_trackers_toggle.setMaximumWidth(120)
            self.show_new_trackers_toggle.setToolTip("Show newly initialized trackers (yellow stars)")
            toggles_row2.addWidget(self.show_new_trackers_toggle)
            
            self.show_hand_tracking_toggle = QPushButton("7. Hand Tracking")
            self.show_hand_tracking_toggle.setCheckable(True)
            self.show_hand_tracking_toggle.setChecked(False)
            self.show_hand_tracking_toggle.clicked.connect(self.toggle_overlays)
            self.show_hand_tracking_toggle.setMaximumWidth(150)
            self.show_hand_tracking_toggle.setToolTip("Show hand detections (purple boxes)")
            toggles_row2.addWidget(self.show_hand_tracking_toggle)
            
            toggles_row2.addStretch()
            calibration_layout.addLayout(toggles_row2)
            
            # Row 3: State and occlusion
            toggles_row3 = QHBoxLayout()
            toggles_row3.setSpacing(5)
            
            self.show_ball_states_toggle = QPushButton("8. Throw/Catch States")
            self.show_ball_states_toggle.setCheckable(True)
            self.show_ball_states_toggle.setChecked(False)
            self.show_ball_states_toggle.clicked.connect(self.toggle_overlays)
            self.show_ball_states_toggle.setMaximumWidth(180)
            self.show_ball_states_toggle.setToolTip("Show ball states (orange indicators with labels)")
            toggles_row3.addWidget(self.show_ball_states_toggle)
            
            self.show_occlusion_toggle = QPushButton("9. Occlusion")
            self.show_occlusion_toggle.setCheckable(True)
            self.show_occlusion_toggle.setChecked(False)
            self.show_occlusion_toggle.clicked.connect(self.toggle_overlays)
            self.show_occlusion_toggle.setMaximumWidth(130)
            self.show_occlusion_toggle.setToolTip("Show occlusion states")
            toggles_row3.addWidget(self.show_occlusion_toggle)
            
            self.show_skeleton_toggle = QPushButton("10. Pose Skeleton")
            self.show_skeleton_toggle.setCheckable(True)
            self.show_skeleton_toggle.setChecked(False)
            self.show_skeleton_toggle.clicked.connect(self.toggle_overlays)
            self.show_skeleton_toggle.setMaximumWidth(150)
            self.show_skeleton_toggle.setToolTip("Show pose estimation keypoints (cyan skeleton)")
            toggles_row3.addWidget(self.show_skeleton_toggle)
            
            toggles_row3.addStretch()
            calibration_layout.addLayout(toggles_row3)
            
            # Row 4: Color tracking and final results
            toggles_row4 = QHBoxLayout()
            toggles_row4.setSpacing(5)
            
            self.show_color_search_toggle = QPushButton("11. Color Search")
            self.show_color_search_toggle.setCheckable(True)
            self.show_color_search_toggle.setChecked(False)
            self.show_color_search_toggle.clicked.connect(self.toggle_overlays)
            self.show_color_search_toggle.setMaximumWidth(150)
            self.show_color_search_toggle.setToolTip("Show color tracking search regions")
            toggles_row4.addWidget(self.show_color_search_toggle)
            
            self.show_color_tracker_toggle = QPushButton("11. Color Tracking")
            self.show_color_tracker_toggle.setCheckable(True)
            self.show_color_tracker_toggle.setChecked(True)
            self.show_color_tracker_toggle.clicked.connect(self.toggle_overlays)
            self.show_color_tracker_toggle.setMaximumWidth(160)
            self.show_color_tracker_toggle.setToolTip("Show color-tracked balls (colored circles)")
            toggles_row4.addWidget(self.show_color_tracker_toggle)
            
            self.show_tracked_boxes_toggle = QPushButton("12. Final Trackers")
            self.show_tracked_boxes_toggle.setCheckable(True)
            self.show_tracked_boxes_toggle.setChecked(False)
            self.show_tracked_boxes_toggle.clicked.connect(self.toggle_overlays)
            self.show_tracked_boxes_toggle.setMaximumWidth(150)
            self.show_tracked_boxes_toggle.setToolTip("Show final persistent tracker boxes (thick green)")
            toggles_row4.addWidget(self.show_tracked_boxes_toggle)
            
            toggles_row4.addStretch()
            calibration_layout.addLayout(toggles_row4)
            
            # Row 5: Unmatched and utility
            toggles_row5 = QHBoxLayout()
            toggles_row5.setSpacing(5)
            
            self.show_unmatched_detections_toggle = QPushButton("13. Unmatched")
            self.show_unmatched_detections_toggle.setCheckable(True)
            self.show_unmatched_detections_toggle.setChecked(True)
            self.show_unmatched_detections_toggle.clicked.connect(self.toggle_overlays)
            self.show_unmatched_detections_toggle.setMaximumWidth(140)
            self.show_unmatched_detections_toggle.setToolTip("Show unmatched detections (yellow boxes)")
            toggles_row5.addWidget(self.show_unmatched_detections_toggle)
            
            self.show_tails_toggle = QPushButton("Show Tails")
            self.show_tails_toggle.setCheckable(True)
            self.show_tails_toggle.setChecked(False)
            self.show_tails_toggle.clicked.connect(self.toggle_overlays)
            self.show_tails_toggle.setMaximumWidth(110)
            self.show_tails_toggle.setToolTip("Show tracker history trails")
            toggles_row5.addWidget(self.show_tails_toggle)
            
            self.hide_video_feed_toggle = QPushButton("Hide Video Feed")
            self.hide_video_feed_toggle.setCheckable(True)
            self.hide_video_feed_toggle.setChecked(False)
            self.hide_video_feed_toggle.clicked.connect(self.toggle_overlays)
            self.hide_video_feed_toggle.setToolTip("Hide the video feed but keep overlays visible")
            self.hide_video_feed_toggle.setMaximumWidth(140)
            toggles_row5.addWidget(self.hide_video_feed_toggle)
            
            toggles_row5.addStretch()
            calibration_layout.addLayout(toggles_row5)

            # --- Tail Length Slider ---
            tail_layout = QHBoxLayout()
            tail_layout.addWidget(QLabel("Tail Length:"))
            self.tail_length_slider = QSlider(Qt.Orientation.Horizontal)
            self.tail_length_slider.setRange(10, 200)
            self.tail_length_slider.setValue(50)
            self.tail_length_slider.valueChanged.connect(self.update_tail_length)
            tail_layout.addWidget(self.tail_length_slider)
            self.tail_length_label = QLabel("50 frames")
            tail_layout.addWidget(self.tail_length_label)
            calibration_layout.addLayout(tail_layout)
            
            # Add Calibration Visualization under Camera Feed
            center_panel_layout.addWidget(calibration_group)
            
            content_layout.addLayout(center_panel_layout, 2)
            
            # Right panel - Tracking Settings and System Status
            # Add Tracking Settings directly (no tab wrapper)
            settings_group = QGroupBox("⚙️ Tracking Settings")
            settings_layout = QVBoxLayout(settings_group)
            
            # Create settings widget and add it directly
            self.settings_widget = CalibrationSettingsWidget(self.udp_client, self.zmq_client, self.hub_instance)
            settings_layout.addWidget(self.settings_widget)
            
            right_panel_layout.addWidget(settings_group)
            
            # System Status section
            system_group = QGroupBox("⚙️ System Status")
            system_layout = QVBoxLayout(system_group)
            
            self.camera_status = QLabel("📷 Camera: Unknown")
            self.engine_status = QLabel("🔧 Engine: Unknown")
            self.mode_status = QLabel("🎯 Mode: Unknown")
            
            system_layout.addWidget(self.camera_status)
            system_layout.addWidget(self.engine_status)
            system_layout.addWidget(self.mode_status)
            
            # Hand tracking
            self.hand_status = QLabel("👋 Hands: 0")
            system_layout.addWidget(self.hand_status)
            
            # IMU status
            self.imu_status = QLabel("📱 IMU: 0 sensors")

            # Web UI controls
            web_ui_layout = QHBoxLayout()
            self.web_ui_button = QPushButton("Start Web UI")
            self.web_ui_button.clicked.connect(self.toggle_web_ui)
            web_ui_layout.addWidget(self.web_ui_button)

            # Screen control buttons
            screen_control_layout = QHBoxLayout()
            self.disable_top_button = QPushButton("Disable Top Screen")
            self.disable_top_button.clicked.connect(self.disable_top_screen)
            screen_control_layout.addWidget(self.disable_top_button)

            self.disable_bottom_button = QPushButton("Disable Bottom Screen")
            self.disable_bottom_button.clicked.connect(self.disable_bottom_screen)
            screen_control_layout.addWidget(self.disable_bottom_button)
            
            system_layout.addLayout(web_ui_layout)
            system_layout.addLayout(screen_control_layout)
            system_layout.addWidget(self.imu_status)

            self.imu_list = QTextEdit()
            self.imu_list.setMaximumHeight(200)
            self.imu_list.setReadOnly(True)
            system_layout.addWidget(self.imu_list)
            
            system_layout.addStretch()
            
            right_panel_layout.addWidget(system_group)
            
            # Add all panels to content layout
            content_layout.addLayout(left_panel_layout)
            content_layout.addLayout(right_panel_layout)
            
            main_layout.addLayout(content_layout)
            
            # Bottom panel - Log
            log_group = QGroupBox("📝 Activity Log")
            log_layout = QVBoxLayout(log_group)
            
            # Add control buttons for the log
            log_controls_layout = QHBoxLayout()
            
            self.log_paused = True  # Start paused by default
            self.pause_log_button = QPushButton("▶ Resume Log")
            self.pause_log_button.setCheckable(True)
            self.pause_log_button.setChecked(True)  # Start checked (paused)
            self.pause_log_button.clicked.connect(self.toggle_log_pause)
            self.pause_log_button.setStyleSheet("""
                QPushButton {
                    background-color: #555;
                    color: white;
                    border: 1px solid #777;
                    padding: 5px;
                    border-radius: 3px;
                }
                QPushButton:checked {
                    background-color: #f44336;
                    border-color: #d32f2f;
                }
            """)
            log_controls_layout.addWidget(self.pause_log_button)
            
            self.clear_log_button = QPushButton("🗑 Clear Log")
            self.clear_log_button.clicked.connect(self.clear_log)
            log_controls_layout.addWidget(self.clear_log_button)
            
            log_controls_layout.addStretch()
            log_layout.addLayout(log_controls_layout)
            
            self.log_text = QTextEdit()
            self.log_text.setMaximumHeight(150)
            self.log_text.setReadOnly(True)
            log_layout.addWidget(self.log_text)
            
            main_layout.addWidget(log_group)
            
            self.apply_dark_theme()
            self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        
        def create_menu_bar(self):
            """Create the menu bar with File, App, and Help menus."""
            menubar = self.menuBar()
            
            # File menu
            file_menu = menubar.addMenu("&File")
            
            # Save Settings action
            save_action = QAction("&Save Settings", self)
            save_action.setShortcut("Ctrl+S")
            save_action.triggered.connect(self.save_settings_dialog)
            file_menu.addAction(save_action)
            
            # Load Settings action
            load_action = QAction("&Load Settings", self)
            load_action.setShortcut("Ctrl+O")
            load_action.triggered.connect(self.load_settings_dialog)
            file_menu.addAction(load_action)
            
            file_menu.addSeparator()
            
            # Manage Color Profiles action
            color_profiles_action = QAction("Manage &Color Profiles", self)
            color_profiles_action.setShortcut("Ctrl+P")
            color_profiles_action.triggered.connect(self.open_color_profile_manager)
            file_menu.addAction(color_profiles_action)
            
            # App menu
            app_menu = menubar.addMenu("&App")
            
            # Recent Apps submenu
            self.recent_apps_menu = app_menu.addMenu("&Recent Apps")
            self.update_recent_apps_menu()
            
            app_menu.addSeparator()
            
            # App Manager action
            app_manager_action = QAction("App &Manager...", self)
            app_manager_action.setShortcut("Ctrl+M")
            app_manager_action.triggered.connect(self.open_app_manager)
            app_menu.addAction(app_manager_action)
        
        def populate_color_profiles(self):
            """Populate the color profile dropdown from the manager."""
            self.color_profile_combo.clear()
            profile_names = self.color_profile_manager.get_profile_names()
            self.color_profile_combo.addItems(profile_names)
        
        def open_color_profile_manager(self):
            """Open the color profile manager dialog."""
            dialog = ColorProfileDialog(self)
            if dialog.exec() == QDialog.DialogCode.Accepted:
                # Reload the color profile manager
                self.color_profile_manager.load_profiles()
                # Refresh the dropdown
                current_selection = self.color_profile_combo.currentText()
                self.populate_color_profiles()
                # Try to restore previous selection
                index = self.color_profile_combo.findText(current_selection)
                if index >= 0:
                    self.color_profile_combo.setCurrentIndex(index)
                self.log_message("✅ Color profiles updated")
                # Refresh the video feed to show updated colors
                if self.last_frame_data:
                    self.update_video_feed(self.last_frame_data)
           
            # Help menu
            help_menu = menubar.addMenu("&Help")
            
            # About action
            about_action = QAction("&About", self)
            about_action.triggered.connect(self.show_about_dialog)
            help_menu.addAction(about_action)
        
        def update_recent_apps_menu(self):
            """Update the Recent Apps submenu with recently used apps."""
            self.recent_apps_menu.clear()
            
            if not hasattr(self.hub_instance, 'app_manager') or not self.hub_instance.app_manager:
                no_apps_action = QAction("No recent apps", self)
                no_apps_action.setEnabled(False)
                self.recent_apps_menu.addAction(no_apps_action)
                return
            
            recent_apps = self.hub_instance.app_manager.get_recent_apps(5)
            
            if not recent_apps:
                no_apps_action = QAction("No recent apps", self)
                no_apps_action.setEnabled(False)
                self.recent_apps_menu.addAction(no_apps_action)
            else:
                for app_id in recent_apps:
                    app_info = self.hub_instance.app_manager.get_app_info(app_id)
                    if app_info:
                        action = QAction(app_info['name'], self)
                        action.triggered.connect(lambda checked, aid=app_id: self.launch_app(aid))
                        self.recent_apps_menu.addAction(action)
        
        def open_app_manager(self):
            """Open the App Manager dialog."""
            if not hasattr(self.hub_instance, 'app_manager') or not self.hub_instance.app_manager:
                QMessageBox.warning(self, "App Manager", "App Manager is not initialized.")
                return
            
            from .app_manager_dialog import AppManagerDialog
            dialog = AppManagerDialog(self.hub_instance.app_manager, self)
            dialog.app_launched.connect(self.on_app_launched)
            dialog.exec()
            
            # Update recent apps menu after dialog closes
            self.update_recent_apps_menu()
        
        def launch_app(self, app_id: str):
            """Launch an app by its ID."""
            if not hasattr(self.hub_instance, 'app_manager') or not self.hub_instance.app_manager:
                QMessageBox.warning(self, "Launch App", "App Manager is not initialized.")
                return
            
            try:
                self.hub_instance.app_manager.launch_app(app_id)
                self.log_message(f"✅ Launched app: {app_id}")
                self.update_recent_apps_menu()
            except Exception as e:
                QMessageBox.critical(self, "Launch Error", f"Failed to launch app '{app_id}':\n{str(e)}")
                self.log_message(f"❌ Failed to launch app '{app_id}': {e}")
        
        def on_app_launched(self, app_id: str):
            """Handle app launched signal from App Manager dialog."""
            self.log_message(f"✅ App launched: {app_id}")
            self.update_recent_apps_menu()
        
        def save_settings_dialog(self):
            """Show file dialog to save calibration settings."""
            if not self.calibration_mode:
                QMessageBox.information(self, "Info", "Please enter Calibration Mode first to save settings.")
                return
            
            filepath, _ = QFileDialog.getSaveFileName(
                self,
                "Save Calibration Settings",
                os.path.join(self.settings_widget.calibration_saves_dir, "calibration_settings.json"),
                "JSON Files (*.json);;All Files (*)"
            )
            
            if filepath:
                if self.settings_widget.save_settings(filepath):
                    QMessageBox.information(self, "Success", f"Settings saved to:\n{filepath}")
                    self.log_message(f"✅ Settings saved to {filepath}")
                else:
                    QMessageBox.critical(self, "Error", "Failed to save settings. Check console for details.")
        
        def load_settings_dialog(self):
            """Show file dialog to load calibration settings."""
            if not self.calibration_mode:
                QMessageBox.information(self, "Info", "Please enter Calibration Mode first to load settings.")
                return
            
            filepath, _ = QFileDialog.getOpenFileName(
                self,
                "Load Calibration Settings",
                self.settings_widget.calibration_saves_dir,
                "JSON Files (*.json);;All Files (*)"
            )
            
            if filepath:
                if self.settings_widget.load_settings(filepath):
                    QMessageBox.information(self, "Success", f"Settings loaded from:\n{filepath}")
                    self.log_message(f"✅ Settings loaded from {filepath}")
                else:
                    QMessageBox.critical(self, "Error", "Failed to load settings. Check console for details.")
        
        def show_about_dialog(self):
            """Show the About dialog."""
            about_text = """
            <h2>JuggleHub</h2>
            <p><b>Version:</b> 1.0.0</p>
            <p><b>Description:</b> A comprehensive juggling analysis system that tracks balls in real-time using computer vision and deep learning.</p>
            <br>
            <p><b>Features:</b></p>
            <ul>
                <li>Real-time ball tracking with YOLO object detection</li>
                <li>ByteTrack multi-object tracking</li>
                <li>Color-based ball identification</li>
                <li>3D position estimation</li>
                <li>Camera calibration tools</li>
                <li>Video recording capabilities</li>
            </ul>
            <br>
            <p><b>Technologies:</b></p>
            <ul>
                <li>Python & PyQt6</li>
                <li>C++ & OpenVINO</li>
                <li>Intel RealSense D455</li>
                <li>ZeroMQ & Protocol Buffers</li>
            </ul>
            <br>
            <p>© 2025 JuggleHub Project</p>
            """
            
            QMessageBox.about(self, "About JuggleHub", about_text)
        
        def apply_dark_theme(self):
            """Apply a dark theme to the UI."""
            self.setStyleSheet("""
                QMainWindow, QDialog { background-color: #2b2b2b; color: #ffffff; }
                QGroupBox {
                    font-weight: bold; border: 2px solid #555555;
                    border-radius: 5px; margin-top: 1ex; padding-top: 10px;
                }
                QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }
                QLabel { color: #ffffff; }
                QTextEdit { background-color: #1e1e1e; border: 1px solid #555555; color: #ffffff; }
                QPushButton {
                    background-color: #555; color: white; border: 1px solid #777;
                    padding: 5px; border-radius: 3px;
                }
                QPushButton:hover { background-color: #666; }
                QPushButton:pressed { background-color: #444; }
                QPushButton:checked { background-color: #007ACC; border-color: #005A9E; }
            """)
        
        def update_frame_data(self, frame_data: juggler_pb2.FrameData):
            self.signal_emitter.frame_received.emit(frame_data)
        
        def _update_ui(self, frame_data: juggler_pb2.FrameData):
            self.last_frame_data = frame_data
            self.frame_count += 1
            self.log_message(f"UI received frame {frame_data.frame_number} with {len(frame_data.balls)} balls.")
            
            # Check for throw/catch events and play sounds if enabled
            if hasattr(frame_data, 'throw_catch_events'):
                for event in frame_data.throw_catch_events:
                    if event.type == juggler_pb2.ThrowCatchEvent.CATCH:
                        # Play color name if enabled, otherwise play beep sound
                        if self.settings_widget.tc_name_on_catch_toggle.isChecked():
                            # Find the color name for this ball
                            color_ball = next((cb for cb in frame_data.color_tracked_balls if cb.logical_id == event.ball_id), None)
                            if color_ball and color_ball.color_name:
                                self.settings_widget.play_color_name(color_ball.color_name)
                                self.log_message(f"🔊 CATCH name '{color_ball.color_name}' played for Ball {event.ball_id}")
                        elif self.settings_widget.tc_sound_on_catch_toggle.isChecked():
                            self.settings_widget.play_system_sound(frequency=800, duration=100)
                            self.log_message(f"🔊 CATCH sound played for Ball {event.ball_id} by Hand {event.hand_id}")
                    elif event.type == juggler_pb2.ThrowCatchEvent.THROW:
                        # Play color name if enabled, otherwise play beep sound
                        if self.settings_widget.tc_name_on_throw_toggle.isChecked():
                            # Find the color name for this ball
                            color_ball = next((cb for cb in frame_data.color_tracked_balls if cb.logical_id == event.ball_id), None)
                            if color_ball and color_ball.color_name:
                                self.settings_widget.play_color_name(color_ball.color_name)
                                self.log_message(f"🔊 THROW name '{color_ball.color_name}' played for Ball {event.ball_id}")
                        elif self.settings_widget.tc_sound_on_throw_toggle.isChecked():
                            self.settings_widget.play_system_sound(frequency=1200, duration=100)
                            self.log_message(f"🔊 THROW sound played for Ball {event.ball_id} by Hand {event.hand_id}")
            
            ball_count = len(frame_data.balls)
            self.ball_count_label.setText(f"Balls detected: {ball_count}")
            
            # Get color map from profile manager for text coloring
            color_name_map = self.color_profile_manager.get_color_map()
            
            # Build HTML formatted text with colored ball information
            ball_html = ""
            # Define a mapping from enum to string for display
            status_map = {
                juggler_pb2.Ball.TRACKED: "Tracked",
                juggler_pb2.Ball.PREDICTED: "Predicted",
                juggler_pb2.Ball.OCCLUDED: "Occluded",
            }

            for ball in frame_data.balls:
                status_str = status_map.get(ball.status, "Unknown")
                
                # Get color information from color_tracked_balls
                color_ball = next((cb for cb in frame_data.color_tracked_balls if cb.logical_id == ball.logical_id), None)
                
                # Determine the color for this ball's text
                if color_ball and color_ball.color_name:
                    qcolor = color_name_map.get(color_ball.color_name.lower(), QColor(255, 255, 255))
                    # Convert QColor to hex for HTML
                    color_hex = qcolor.name()
                    state_str = "HELD" if color_ball.associated_wrist_id >= 0 else "IN AIR"
                    hand_str = f" by {'LEFT' if color_ball.associated_wrist_id == 0 else 'RIGHT'}" if color_ball.associated_wrist_id >= 0 else ""
                    color_info = f"Color: {color_ball.color_name.upper()}, State: {state_str}{hand_str}"
                else:
                    color_hex = "#ffffff"
                    color_info = "Color: Unknown, State: Unknown"
                
                # Create HTML formatted text with color
                ball_html += f'<span style="color: {color_hex};">'
                ball_html += f'Ball {ball.logical_id} ({status_str}): 3D({ball.position.x:.3f}, {ball.position.y:.3f}, {ball.position.z:.3f})<br>'
                ball_html += f'&nbsp;&nbsp;{color_info}<br>'
                
                # Determine ML detection status - check if there's an actual YOLO detection box
                # by looking for a matching raw_detection near this ball's position
                has_yolo_detection = False
                yolo_class_id = -1
                
                # Check if any raw_detection is close to this ball's 2D position
                if ball.projected_pos_2d and ball.projected_pos_2d.x > 0 and ball.projected_pos_2d.y > 0:
                    for raw_det in frame_data.raw_detections:
                        # Check if detection box contains the ball's projected position
                        det_center_x = raw_det.x + raw_det.width / 2
                        det_center_y = raw_det.y + raw_det.height / 2
                        ball_x = ball.projected_pos_2d.x
                        ball_y = ball.projected_pos_2d.y
                        
                        # Check if ball position is within detection box
                        if (raw_det.x <= ball_x <= raw_det.x + raw_det.width and
                            raw_det.y <= ball_y <= raw_det.y + raw_det.height):
                            has_yolo_detection = True
                            yolo_class_id = raw_det.class_id
                            break
                
                # Display ML detection status based on whether YOLO actually sees it
                if has_yolo_detection:
                    if yolo_class_id == 0:
                        ml_status = "ball (in-air)"
                    elif yolo_class_id == 1:
                        ml_status = "ball_held"
                    else:
                        ml_status = f"class_{yolo_class_id}"
                else:
                    # No YOLO detection box found
                    ml_status = "gone"
                
                ball_html += f'&nbsp;&nbsp;ML Detection: {ml_status}<br>'
                
                # Add distance to nearest wrist
                if hasattr(ball, 'distance_to_nearest_wrist') and ball.distance_to_nearest_wrist >= 0:
                    dist_cm = ball.distance_to_nearest_wrist * 100  # Convert m to cm
                    ball_html += f'&nbsp;&nbsp;Distance to Wrist: {dist_cm:.1f}cm<br>'
                else:
                    ball_html += f'&nbsp;&nbsp;Distance to Wrist: N/A<br>'
                
                ball_html += '</span>'
                
                # Update tracker history using logical_id
                if ball.logical_id not in self.tracker_history:
                    self.tracker_history[ball.logical_id] = []
                
                # Only add to history if the point is valid
                if ball.projected_pos_2d.x > 0 and ball.projected_pos_2d.y > 0:
                    self.tracker_history[ball.logical_id].append((ball.projected_pos_2d.x, ball.projected_pos_2d.y))
                
                # Prune history to tail length
                max_len = self.tail_length_slider.value()
                while len(self.tracker_history[ball.logical_id]) > max_len:
                    self.tracker_history[ball.logical_id].pop(0)

            self.ball_list.setHtml(ball_html)

            # Always try to update the video feed if the widget is visible
            if self.video_group.isVisible() and frame_data.color_image_b64:
                self.update_video_feed(frame_data)
            elif self.video_group.isVisible():
                self.log_message(f"UI: Video feed is visible but frame {frame_data.frame_number} has no image data.")
            
            if self.settings_widget:
                is_camera_running = "Running" in self.settings_widget.camera_status_label.text()
                self.settings_widget.update_ir_status(frame_data.ir_projector_active and is_camera_running)

            if frame_data.HasField('status'):
                status = frame_data.status
                self.camera_status.setText(f"📷 Camera: {'Connected' if status.camera_connected else 'Disconnected'}")
                self.engine_status.setText(f"🔧 Engine: {'Running' if status.engine_running else 'Stopped'}")
                self.mode_status.setText(f"🎯 Mode: {status.mode}")
                if status.error_message: self.log_message(f"❌ Error: {status.error_message}")
            
            self.hand_status.setText(f"👋 Hands: {len(frame_data.hands)}")
            self.imu_status.setText(f"📱 IMU: {len(frame_data.imu_data)} sensors")
            imu_text = ""
            for imu in frame_data.imu_data:
                imu_text += f"Watch: {imu.watch_name} ({imu.watch_ip})\n"
                imu_text += f"  Accel: ({imu.acceleration.x:.2f}, {imu.acceleration.y:.2f}, {imu.acceleration.z:.2f})\n"
                imu_text += f"  Gyro:  ({imu.gyroscope.x:.2f}, {imu.gyroscope.y:.2f}, {imu.gyroscope.z:.2f})\n"
            self.imu_list.setPlainText(imu_text)
            self.status_label.setText(f"✅ Receiving data - Frame {frame_data.frame_number}")
        
        def _periodic_update(self):
            elapsed = time.time() - self.start_time
            fps = self.frame_count / elapsed if elapsed > 0 else 0
            self.fps_label.setText(f"FPS: {fps:.1f}")
            self.frame_count_label.setText(f"Frames: {self.frame_count}")
            if self.last_frame_data:
                time_since_last = (time.time() * 1000000 - self.last_frame_data.timestamp_us) / 1000000
                if time_since_last > 2.0: self.status_label.setText("⚠️ No data received recently")
        
        def toggle_log_pause(self):
            """Toggle the activity log pause state."""
            self.log_paused = self.pause_log_button.isChecked()
            if self.log_paused:
                self.pause_log_button.setText("▶ Resume Log")
            else:
                self.pause_log_button.setText("⏸ Pause Log")
        
        def clear_log(self):
            """Clear all messages from the activity log."""
            self.log_text.clear()
        
        def log_message(self, message: str):
            # Don't add messages if log is paused
            if self.log_paused:
                return
            
            timestamp = time.strftime("%H:%M:%S")
            self.log_text.append(f"[{timestamp}] {message}")
            if self.log_text.document().blockCount() > 100:
                cursor = self.log_text.textCursor()
                cursor.movePosition(cursor.MoveOperation.Start)
                cursor.select(cursor.SelectionType.BlockUnderCursor)
                cursor.removeSelectedText()
        
        # Calibration mode is now always on - no toggle needed

        def toggle_overlays(self):
            if self.last_frame_data: self.update_video_feed(self.last_frame_data)

        def update_video_feed(self, frame_data: juggler_pb2.FrameData):
            self.log_message(f"UI: update_video_feed called for frame {frame_data.frame_number}.")
            self.log_message(f"UI: Frame has {len(frame_data.hands)} hands, {len(frame_data.balls)} balls")
            if not frame_data.color_image_b64:
                self.log_message(f"UI ERROR: Frame {frame_data.frame_number} has no color_image_b64 data.")
                return

            image = QImage()
            load_success = image.loadFromData(frame_data.color_image_b64, "JPEG")

            if not load_success:
                self.log_message(f"UI ERROR: QImage.loadFromData failed for frame {frame_data.frame_number}. Image data size: {len(frame_data.color_image_b64)} bytes.")
                # Optionally, save the bad frame for debugging
                # with open(f"bad_frame_{frame_data.frame_number}.jpg", "wb") as f:
                #     f.write(frame_data.color_image_b64)
                return
            
            self.log_message(f"UI: Frame {frame_data.frame_number} loaded into QImage successfully. Size: {image.width()}x{image.height()}.")

            # Check if video feed should be hidden
            if self.hide_video_feed_toggle.isChecked():
                # Create a blank black image with the same dimensions
                pixmap = QPixmap(image.width(), image.height())
                pixmap.fill(QColor(0, 0, 0))  # Fill with black
            else:
                # Use the actual video frame
                pixmap = QPixmap.fromImage(image)
            
            painter = QPainter(pixmap)
            
            # --- Draw Kalman Predictions (Step 2) ---
            # NEW: Matches the saved visualization style from Engine.cpp
            if self.show_kalman_predictions_toggle.isChecked():
                # Debug logging
                if len(frame_data.kalman_predictions) > 0:
                    self.log_message(f"[KALMAN VIZ] Rendering {len(frame_data.kalman_predictions)} Kalman predictions")
                
                # Get camera intrinsics
                fx = 385.0  # Approximate D455 intrinsics
                fy = 385.0
                ppx = 320.0
                ppy = 240.0
                
                for i, pred in enumerate(frame_data.kalman_predictions):
                    # Project 3D predicted position to 2D
                    if pred.predicted_pos.z <= 0:
                        continue
                    
                    pred_x = int((pred.predicted_pos.x * fx) / pred.predicted_pos.z + ppx)
                    pred_y = int((pred.predicted_pos.y * fy) / pred.predicted_pos.z + ppy)
                    
                    if i == 0:  # Log first prediction for debugging
                        self.log_message(f"[KALMAN VIZ]   Pred {i}: 3D({pred.predicted_pos.x:.3f}, {pred.predicted_pos.y:.3f}, {pred.predicted_pos.z:.3f}) -> 2D({pred_x}, {pred_y})")
                    
                    # Get prediction radius - use a default if not available
                    # The radius represents the search region uncertainty in meters
                    uncertainty_meters = 0.15  # Default 15cm radius
                    
                    # Project uncertainty to pixel space
                    uncertainty_pixels = (uncertainty_meters * fx) / pred.predicted_pos.z
                    radius = int(uncertainty_pixels)
                    
                    # Clamp radius to reasonable bounds
                    radius = max(20, min(radius, 150))
                    
                    # Choose color based on ball state (in freefall vs held)
                    if pred.is_in_freefall:
                        circle_color = QColor(255, 255, 100)  # Cyan-ish for in-flight balls (with gravity)
                    else:
                        circle_color = QColor(100, 100, 255)  # Red-ish for held balls (no gravity)
                    
                    # Draw semi-transparent circle showing prediction search region
                    # Create a temporary pixmap for the overlay effect
                    overlay = pixmap.copy()
                    overlay_painter = QPainter(overlay)
                    overlay_painter.setPen(QPen(circle_color, 2, Qt.PenStyle.SolidLine))
                    overlay_painter.setBrush(Qt.BrushStyle.NoBrush)
                    overlay_painter.drawEllipse(pred_x - radius, pred_y - radius, radius * 2, radius * 2)
                    overlay_painter.end()
                    
                    # Blend the overlay with the main pixmap (50% transparency)
                    painter.setOpacity(0.5)
                    painter.drawPixmap(0, 0, overlay)
                    painter.setOpacity(1.0)
                    
                    # Draw center point
                    painter.setPen(QPen(circle_color, 1))
                    painter.setBrush(QBrush(circle_color))
                    painter.drawEllipse(pred_x - 4, pred_y - 4, 8, 8)
                    
                    # Draw label with history size (if available)
                    # For now, just show the logical ID
                    label = f"P{pred.logical_id}"
                    
                    # Draw label with black outline for visibility
                    painter.setFont(QFont("Arial", 10))
                    painter.setPen(QPen(QColor(0, 0, 0), 3))
                    painter.drawText(pred_x + 10, pred_y - 10, label)
                    painter.setPen(QPen(circle_color))
                    painter.drawText(pred_x + 10, pred_y - 10, label)
            
            # --- Draw YOLO Detections (Step 3) ---
            if self.show_raw_detections_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 0, 0, 100), 2)) # Semi-transparent red
                for det in frame_data.raw_detections:
                    painter.drawRect(int(det.x), int(det.y), int(det.width), int(det.height))
            
            # --- Draw Filtered Detections (Step 4) ---
            if self.show_filtered_detections_toggle.isChecked():
                painter.setPen(QPen(QColor(128, 128, 128, 150), 2))  # Gray
                for det in frame_data.filtered_detections:
                    # Draw crossed-out box
                    x, y, w, h = int(det.box.x), int(det.box.y), int(det.box.width), int(det.box.height)
                    painter.drawRect(x, y, w, h)
                    painter.drawLine(x, y, x + w, y + h)
                    painter.drawLine(x + w, y, x, y + h)
            
            # --- Draw 3D Matching/Associations (Step 5) ---
            if self.show_associations_toggle.isChecked():
                painter.setPen(QPen(QColor(0, 255, 0, 200), 2))  # Green lines
                painter.setFont(QFont("Arial", 9, QFont.Weight.Bold))
                
                # Debug logging
                if len(frame_data.tracker_associations) > 0:
                    self.log_message(f"[3D MATCH VIZ] Rendering {len(frame_data.tracker_associations)} associations")
                
                for i, assoc in enumerate(frame_data.tracker_associations):
                    # Project 3D positions to 2D for visualization
                    # Get camera intrinsics
                    fx = frame_data.intrinsics.fx if frame_data.HasField('intrinsics') else 385.0
                    fy = frame_data.intrinsics.fy if frame_data.HasField('intrinsics') else 385.0
                    ppx = frame_data.intrinsics.ppx if frame_data.HasField('intrinsics') else 320.0
                    ppy = frame_data.intrinsics.ppy if frame_data.HasField('intrinsics') else 240.0
                    
                    # Project tracker position
                    if assoc.tracker_pos.z > 0:
                        tracker_x = int((assoc.tracker_pos.x * fx) / assoc.tracker_pos.z + ppx)
                        tracker_y = int((assoc.tracker_pos.y * fy) / assoc.tracker_pos.z + ppy)
                    else:
                        continue
                    
                    # Project detection position
                    if assoc.detection_pos.z > 0:
                        det_x = int((assoc.detection_pos.x * fx) / assoc.detection_pos.z + ppx)
                        det_y = int((assoc.detection_pos.y * fy) / assoc.detection_pos.z + ppy)
                    else:
                        continue
                    
                    if i == 0:  # Log first association for debugging
                        self.log_message(f"[3D MATCH VIZ]   Assoc {i}: Tracker {assoc.tracker_id} -> Det {assoc.detection_index}, dist={assoc.distance_3d:.3f}m")
                        self.log_message(f"[3D MATCH VIZ]   Tracker 2D: ({tracker_x}, {tracker_y}), Det 2D: ({det_x}, {det_y})")
                    
                    # Draw line from tracker to detection
                    painter.drawLine(tracker_x, tracker_y, det_x, det_y)
                    
                    # Draw distance label at midpoint
                    mid_x = (tracker_x + det_x) // 2
                    mid_y = (tracker_y + det_y) // 2
                    
                    # Draw background for text
                    distance_cm = assoc.distance_3d * 100  # Convert to cm
                    label_text = f"T{assoc.tracker_id}→D{assoc.detection_index} ({distance_cm:.1f}cm)"
                    
                    painter.setPen(QPen(QColor(0, 0, 0, 200)))
                    painter.setBrush(QBrush(QColor(0, 0, 0, 150)))
                    text_width = 120
                    text_height = 18
                    painter.drawRect(mid_x - text_width//2, mid_y - text_height//2, text_width, text_height)
                    
                    # Draw text
                    painter.setPen(QPen(QColor(0, 255, 0)))
                    painter.drawText(mid_x - text_width//2 + 5, mid_y + 5, label_text)
                    
                    # Reset pen for next line
                    painter.setPen(QPen(QColor(0, 255, 0, 200), 2))
            
            # --- Draw New Trackers/Auto-Init (Step 6) ---
            if self.show_new_trackers_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 255, 0, 200), 3))  # Bright yellow
                painter.setBrush(QBrush(QColor(255, 255, 0, 100)))
                for ball in frame_data.balls:
                    if hasattr(ball, 'is_new_tracker') and ball.is_new_tracker:
                        # Draw star marker
                        center_x = int(ball.projected_pos_2d.x)
                        center_y = int(ball.projected_pos_2d.y)
                        size = 15
                        # Draw a simple star shape
                        painter.drawEllipse(center_x - size, center_y - size, size * 2, size * 2)
                        painter.drawText(center_x + size + 5, center_y, "NEW")

            # --- Draw Unmatched Detections (Step 13) ---
            if self.show_unmatched_detections_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 255, 0, 150), 2)) # Yellow
                for det in frame_data.unmatched_detections:
                    painter.drawRect(int(det.x), int(det.y), int(det.width), int(det.height))

            # --- Draw ByteTrack Boxes (Step 12) ---
            if self.show_tracked_boxes_toggle.isChecked():
                painter.setPen(QPen(QColor(0, 255, 0, 200), 3))  # Thick green
                for obj in frame_data.balls:
                    if obj.status == juggler_pb2.Ball.TRACKED:
                        bbox = obj.bounding_box_2d
                        painter.drawRect(int(bbox.x), int(bbox.y), int(bbox.width), int(bbox.height))

            # --- Draw Hand Tracking (Step 7) ---
            if self.show_hand_tracking_toggle.isChecked():
                painter.setPen(QPen(QColor(128, 0, 128, 150), 2))  # Purple
                painter.setBrush(Qt.BrushStyle.NoBrush)
                for hand in frame_data.hands:
                    # Draw purple box around hand detection
                    if hasattr(hand, 'bounding_box_2d'):
                        bbox = hand.bounding_box_2d
                        painter.drawRect(int(bbox.x), int(bbox.y), int(bbox.width), int(bbox.height))
                    # Draw wrist position
                    center_x, center_y = int(hand.position_2d.x), int(hand.position_2d.y)
                    painter.drawEllipse(center_x - 5, center_y - 5, 10, 10)
                    # Label
                    painter.setPen(QPen(QColor(255, 255, 255)))
                    painter.setFont(QFont("Arial", 10, QFont.Weight.Bold))
                    side_label = "L" if hand.side == "left" else "R"
                    painter.drawText(center_x + 10, center_y, f"Hand-{side_label}")
                    painter.setPen(QPen(QColor(128, 0, 128, 150), 2))
            
            # --- Draw Ball States (Step 8) ---
            if self.show_ball_states_toggle.isChecked():
                painter.setFont(QFont("Arial", 9, QFont.Weight.Bold))
                for ball in frame_data.balls:
                    if hasattr(ball, 'throw_catch_state'):
                        center_x = int(ball.projected_pos_2d.x)
                        center_y = int(ball.projected_pos_2d.y)
                        
                        # Draw state indicator
                        state_text = ""
                        state_color = QColor(255, 165, 0)  # Orange
                        
                        if ball.throw_catch_state == juggler_pb2.Ball.THROWN:
                            state_text = "THROW"
                            state_color = QColor(255, 100, 0)
                        elif ball.throw_catch_state == juggler_pb2.Ball.CAUGHT:
                            state_text = "CATCH"
                            state_color = QColor(0, 255, 100)
                        elif ball.throw_catch_state == juggler_pb2.Ball.IN_FLIGHT:
                            state_text = "FLIGHT"
                            state_color = QColor(100, 150, 255)
                        
                        if state_text:
                            # Draw background rectangle
                            painter.setBrush(QBrush(QColor(0, 0, 0, 180)))
                            painter.setPen(QPen(state_color, 2))
                            text_width = 60
                            text_height = 20
                            painter.drawRect(center_x - text_width//2, center_y - 30, text_width, text_height)
                            
                            # Draw text
                            painter.setPen(QPen(state_color))
                            painter.drawText(center_x - text_width//2 + 5, center_y - 15, state_text)
            
            # --- Draw Occlusion (Step 9) ---
            if self.show_occlusion_toggle.isChecked():
                for ball in frame_data.balls:
                    if ball.status == juggler_pb2.Ball.OCCLUDED:
                        center_x = int(ball.projected_pos_2d.x)
                        center_y = int(ball.projected_pos_2d.y)
                        
                        # Draw red X over occluded balls
                        painter.setPen(QPen(QColor(255, 0, 0, 200), 3))
                        size = 15
                        painter.drawLine(center_x - size, center_y - size, center_x + size, center_y + size)
                        painter.drawLine(center_x + size, center_y - size, center_x - size, center_y + size)
                        
                        # Draw "OCCLUDED" label
                        painter.setFont(QFont("Arial", 9, QFont.Weight.Bold))
                        painter.setPen(QPen(QColor(255, 0, 0)))
                        painter.drawText(center_x + 20, center_y, "OCCLUDED")
            
            # --- Draw Color Search Regions (Step 11) ---
            if self.show_color_search_toggle.isChecked():
                painter.setPen(QPen(QColor(255, 255, 0, 100), 1, Qt.PenStyle.DotLine))  # Yellow dotted
                painter.setBrush(Qt.BrushStyle.NoBrush)
                for ball in frame_data.balls:
                    if hasattr(ball, 'color_search_region'):
                        region = ball.color_search_region
                        painter.drawRect(int(region.x), int(region.y), int(region.width), int(region.height))
            
            # --- Draw Color Trackers (Step 11 - NEW SIMPLIFIED SYSTEM) ---
            if self.show_color_tracker_toggle.isChecked():
                # Get color map from profile manager
                color_name_map = self.color_profile_manager.get_color_map()
                
                for color_ball in frame_data.color_tracked_balls:
                    if not color_ball.is_active:
                        continue
                    
                    center_x, center_y = int(color_ball.pixel_pos.x), int(color_ball.pixel_pos.y)
                    # Use the actual color name from the tracker, fallback to white if unknown
                    color = color_name_map.get(color_ball.color_name.lower(), QColor(255, 255, 255))
                    radius = 12
                    
                    # Render based on wrist association
                    if color_ball.associated_wrist_id >= 0:
                        # Ball is near a wrist - draw with dashed outline
                        painter.setBrush(Qt.BrushStyle.NoBrush)
                        pen = QPen(color, 3)
                        pen.setStyle(Qt.PenStyle.DashLine)
                        painter.setPen(pen)
                    else:
                        # Ball is being tracked normally - solid fill
                        painter.setBrush(QBrush(color))
                        # Use thicker black border for white balls to make them visible
                        if color_ball.color_name.lower() == 'white':
                            painter.setPen(QPen(QColor(0, 0, 0), 3))
                        else:
                            painter.setPen(QPen(QColor(0, 0, 0, 100), 1))
                    
                    painter.drawEllipse(center_x - radius, center_y - radius, radius * 2, radius * 2)
                    
                    # Draw label with background for better visibility
                    painter.setFont(QFont("Arial", 10, QFont.Weight.Bold))
                    label = f"{color_ball.color_name} ({color_ball.logical_id})"
                    pos_label = f"({color_ball.world_pos.z:.2f}m)"
                    
                    # Use black text with white outline for white balls, white text with black outline for others
                    if color_ball.color_name.lower() == 'white':
                        # Draw text shadow for white balls
                        painter.setPen(QPen(QColor(255, 255, 255)))
                        painter.drawText(center_x + 14, center_y - 1, label)
                        painter.drawText(center_x + 16, center_y + 1, label)
                        painter.drawText(center_x + 14, center_y + 14, pos_label)
                        painter.drawText(center_x + 16, center_y + 16, pos_label)
                        # Draw actual text
                        painter.setPen(QPen(QColor(0, 0, 0)))
                        painter.drawText(center_x + 15, center_y, label)
                        painter.drawText(center_x + 15, center_y + 15, pos_label)
                    else:
                        painter.setPen(QPen(QColor(255, 255, 255)))
                        painter.drawText(center_x + 15, center_y, label)
                        painter.drawText(center_x + 15, center_y + 15, pos_label)
                    
                    # Show wrist association if present
                    if color_ball.associated_wrist_id >= 0:
                        wrist_label = "L" if color_ball.associated_wrist_id == 0 else "R"
                        if color_ball.color_name.lower() == 'white':
                            painter.setPen(QPen(QColor(0, 0, 0)))
                        else:
                            painter.setPen(QPen(QColor(255, 255, 255)))
                        painter.drawText(center_x + 15, center_y + 30, f"[{wrist_label}]")

            # --- Draw Tracker Tails ---
            if self.show_tails_toggle.isChecked():
                for ball_id, history in self.tracker_history.items():
                    if len(history) > 1:
                        # Find the corresponding ball to get its color
                        ball_for_tail = next((b for b in frame_data.balls if b.logical_id == ball_id), None)
                        if ball_for_tail:
                            color = self.get_average_color(image, ball_for_tail.bounding_box_2d)
                            pen = QPen(color, 2)
                            
                            # Draw lines between consecutive points in the history
                            for i in range(len(history) - 1):
                                p1 = history[i]
                                p2 = history[i+1]
                                if p1[0] > 0 and p1[1] > 0 and p2[0] > 0 and p2[1] > 0: # Check for valid points
                                    painter.setPen(pen)
                                    painter.drawLine(int(p1[0]), int(p1[1]), int(p2[0]), int(p2[1]))

            # --- Draw Pose Skeleton (Step 10) ---
            if self.show_skeleton_toggle.isChecked():
                self.log_message(f"UI: Drawing skeleton for {len(frame_data.hands)} hands")
                
                # Get frame dimensions for bounds checking
                frame_width = pixmap.width()
                frame_height = pixmap.height()
                
                # Draw hand wrist markers with bright cyan
                painter.setPen(QPen(QColor(0, 255, 255), 4)) # Cyan, thick line
                painter.setBrush(Qt.BrushStyle.NoBrush)
                for hand in frame_data.hands:
                    # Validate wrist position before drawing
                    if hand.position_2d.x > 0 and hand.position_2d.y > 0 and \
                       hand.position_2d.x < frame_width and hand.position_2d.y < frame_height:
                        # Draw a large circle for high visibility
                        center_x, center_y = int(hand.position_2d.x), int(hand.position_2d.y)
                        radius = 20 # Large radius for visibility
                        painter.drawEllipse(center_x - radius, center_y - radius, radius * 2, radius * 2)
                        
                        # Draw hand side label
                        painter.setPen(QPen(QColor(255, 255, 255)))
                        painter.setFont(QFont("Arial", 12, QFont.Weight.Bold))
                        side_label = "L" if hand.side == "left" else "R"
                        painter.drawText(center_x - 5, center_y + 5, side_label)
                        
                        # Reset pen for keypoints
                        painter.setPen(QPen(QColor(0, 255, 255), 4))
                
                # Draw all body keypoints
                painter.setPen(QPen(QColor(0, 255, 255, 200), 3)) # Cyan for skeleton
                painter.setBrush(QBrush(QColor(0, 255, 255, 150)))
                for hand in frame_data.hands:
                    self.log_message(f"UI: Hand has {len(hand.keypoints)} keypoints")
                    for i, kp in enumerate(hand.keypoints):
                        # Validate keypoint position and confidence
                        if kp.confidence > 0.3 and \
                           kp.pos_2d.x > 0 and kp.pos_2d.y > 0 and \
                           kp.pos_2d.x < frame_width and kp.pos_2d.y < frame_height:
                            self.log_message(f"UI: Drawing keypoint {i} at ({kp.pos_2d.x:.1f}, {kp.pos_2d.y:.1f})")
                            # Draw filled circle for keypoint
                            painter.drawEllipse(int(kp.pos_2d.x) - 4, int(kp.pos_2d.y) - 4, 8, 8)
                        else:
                            self.log_message(f"UI: Skipping keypoint {i} (confidence {kp.confidence:.2f} or invalid position)")
                    
                    # Draw skeleton connections if we have enough keypoints
                    if len(hand.keypoints) >= 17:  # YOLO pose has 17 keypoints
                        # Define skeleton connections (COCO format)
                        skeleton_pairs = [
                            (5, 6), (5, 7), (7, 9), (6, 8), (8, 10),  # Arms
                            (5, 11), (6, 12), (11, 12),  # Torso
                            (11, 13), (13, 15), (12, 14), (14, 16)  # Legs
                        ]
                        
                        painter.setPen(QPen(QColor(0, 255, 255, 150), 2))
                        for start_idx, end_idx in skeleton_pairs:
                            if start_idx < len(hand.keypoints) and end_idx < len(hand.keypoints):
                                kp_start = hand.keypoints[start_idx]
                                kp_end = hand.keypoints[end_idx]
                                # Validate BOTH keypoints before drawing line
                                if kp_start.confidence > 0.3 and kp_end.confidence > 0.3 and \
                                   kp_start.pos_2d.x > 0 and kp_start.pos_2d.y > 0 and \
                                   kp_end.pos_2d.x > 0 and kp_end.pos_2d.y > 0 and \
                                   kp_start.pos_2d.x < frame_width and kp_start.pos_2d.y < frame_height and \
                                   kp_end.pos_2d.x < frame_width and kp_end.pos_2d.y < frame_height:
                                    painter.drawLine(
                                        int(kp_start.pos_2d.x), int(kp_start.pos_2d.y),
                                        int(kp_end.pos_2d.x), int(kp_end.pos_2d.y)
                                    )

            painter.end()
            self.video_pixmap_item.setPixmap(pixmap)
            
            # Update scene rect to match pixmap size exactly
            self.video_scene.setSceneRect(self.video_pixmap_item.boundingRect())
            self.video_view.fitInView(self.video_pixmap_item, Qt.AspectRatioMode.KeepAspectRatio)

        def update_tail_length(self, value):
            self.tail_length_label.setText(f"{value} frames")
            # The actual tail length will be used during rendering.
            if self.last_frame_data: self.update_video_feed(self.last_frame_data)

        def get_average_color(self, image: QImage, bbox: juggler_pb2.BoundingBox2D) -> QColor:
            """Calculates the average color within a bounding box."""
            x, y, w, h = int(bbox.x), int(bbox.y), int(bbox.width), int(bbox.height)
            
            # Clamp the bounding box to the image dimensions.
            x = max(0, x)
            y = max(0, y)
            w = min(w, image.width() - x)
            h = min(h, image.height() - y)

            if w <= 0 or h <= 0:
                return QColor(255, 255, 255) # Return white if the box is invalid.

            total_r, total_g, total_b = 0, 0, 0
            pixel_count = 0
            
            for i in range(x, x + w):
                for j in range(y, y + h):
                    pixel_color = image.pixelColor(i, j)
                    total_r += pixel_color.red()
                    total_g += pixel_color.green()
                    total_b += pixel_color.blue()
                    pixel_count += 1
            
            if pixel_count == 0:
                return QColor(255, 255, 255) # Return white if no pixels were processed.

            return QColor(total_r // pixel_count, total_g // pixel_count, total_b // pixel_count)

        def record_clip(self):
            self.log_message("Sending record command to engine...")
            
            # Create visualization states from current toggle states
            viz_states = juggler_pb2.VisualizationStates()
            viz_states.show_kalman_predictions = self.show_kalman_predictions_toggle.isChecked()
            viz_states.show_raw_detections = self.show_raw_detections_toggle.isChecked()
            viz_states.show_filtered_detections = self.show_filtered_detections_toggle.isChecked()
            viz_states.show_associations = self.show_associations_toggle.isChecked()
            viz_states.show_new_trackers = self.show_new_trackers_toggle.isChecked()
            viz_states.show_hand_tracking = self.show_hand_tracking_toggle.isChecked()
            viz_states.show_ball_states = self.show_ball_states_toggle.isChecked()
            viz_states.show_occlusion = self.show_occlusion_toggle.isChecked()
            viz_states.show_skeleton = self.show_skeleton_toggle.isChecked()
            viz_states.show_color_search = self.show_color_search_toggle.isChecked()
            viz_states.show_color_tracker = self.show_color_tracker_toggle.isChecked()
            viz_states.show_tracked_boxes = self.show_tracked_boxes_toggle.isChecked()
            viz_states.show_unmatched_detections = self.show_unmatched_detections_toggle.isChecked()
            viz_states.show_tails = self.show_tails_toggle.isChecked()
            
            command = juggler_pb2.CommandRequest(
                type=juggler_pb2.CommandRequest.CommandType.RECORD_START,
                record_with_yolo_boxes=self.show_raw_detections_toggle.isChecked(),
                record_with_bytetrack_boxes=self.show_tracked_boxes_toggle.isChecked()
            )
            command.visualization_states.CopyFrom(viz_states)
            
            try:
                response = self.zmq_client.send_command(command)
                self.log_message(f"✅ Record command acknowledged: {response.message}" if response.success else f"❌ Record command failed: {response.message}")
            except Exception as e:
                self.log_message(f"❌ Error sending record command: {e}")

        def toggle_continuous_recording(self):
            is_starting = not self.is_continuous_recording
            self.log_message(f"{'Starting' if is_starting else 'Stopping'} continuous recording...")
            command_type = juggler_pb2.CommandRequest.CommandType.RECORD_CONTINUOUS_START if is_starting else juggler_pb2.CommandRequest.CommandType.RECORD_CONTINUOUS_STOP
            
            command = juggler_pb2.CommandRequest(
                type=command_type,
                record_with_yolo_boxes=self.show_raw_detections_toggle.isChecked(),
                record_with_bytetrack_boxes=self.show_tracked_boxes_toggle.isChecked()
            )
            
            # Add visualization states if starting recording
            if is_starting:
                viz_states = juggler_pb2.VisualizationStates()
                viz_states.show_kalman_predictions = self.show_kalman_predictions_toggle.isChecked()
                viz_states.show_raw_detections = self.show_raw_detections_toggle.isChecked()
                viz_states.show_filtered_detections = self.show_filtered_detections_toggle.isChecked()
                viz_states.show_associations = self.show_associations_toggle.isChecked()
                viz_states.show_new_trackers = self.show_new_trackers_toggle.isChecked()
                viz_states.show_hand_tracking = self.show_hand_tracking_toggle.isChecked()
                viz_states.show_ball_states = self.show_ball_states_toggle.isChecked()
                viz_states.show_occlusion = self.show_occlusion_toggle.isChecked()
                viz_states.show_skeleton = self.show_skeleton_toggle.isChecked()
                viz_states.show_color_search = self.show_color_search_toggle.isChecked()
                viz_states.show_color_tracker = self.show_color_tracker_toggle.isChecked()
                viz_states.show_tracked_boxes = self.show_tracked_boxes_toggle.isChecked()
                viz_states.show_unmatched_detections = self.show_unmatched_detections_toggle.isChecked()
                viz_states.show_tails = self.show_tails_toggle.isChecked()
                command.visualization_states.CopyFrom(viz_states)
            
            try:
                response = self.zmq_client.send_command(command)
                if response.success:
                    self.is_continuous_recording = is_starting
                    self.continuous_record_button.setText("Stop Recording" if is_starting else "Start Recording")
                    self.continuous_record_button.setChecked(is_starting)
                    self.recording_status.setText(f"● {'Recording' if is_starting else 'Not Recording'}")
                    self.recording_status.setStyleSheet(f"color: {'#f44336' if is_starting else '#666666'}; font-weight: bold;")
                    self.log_message(f"✅ Continuous recording {'started' if is_starting else 'stopped'}: {response.message}")
                else:
                    self.log_message(f"❌ Failed to {'start' if is_starting else 'stop'} continuous recording: {response.message}")
            except Exception as e:
                self.log_message(f"❌ Error {'starting' if is_starting else 'stopping'} continuous recording: {e}")
        
        def keyPressEvent(self, event):
            if event.key() == Qt.Key.Key_R:
                self.log_message("🎹 'R' key pressed - triggering recording")
                self.record_clip()
            else:
                super().keyPressEvent(event)
        
        def get_latest_frame(self):
            """Returns the latest video frame as a numpy array."""
            if self.last_frame_data and self.last_frame_data.color_image_b64:
                # Use np.frombuffer which is more direct for bytes
                nparr = np.frombuffer(self.last_frame_data.color_image_b64, np.uint8)
                # Decode the image
                img_np = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
                return img_np
            return None

        def toggle_web_ui(self):
            if not self.hub_instance.web_ui.is_running:
                self.hub_instance.web_ui.start()
                self.web_ui_button.setText("Stop Web UI")
                self.show_qr_code()
            else:
                self.hub_instance.web_ui.stop()
                self.web_ui_button.setText("Start Web UI")

        def show_qr_code(self):
            dialog = QDialog(self)
            dialog.setWindowTitle("Scan QR Code")
            layout = QVBoxLayout_Dialog(dialog)
            qr_label = QLabel()
            url = f"http://{self.hub_instance.web_ui.get_ip_address()}:{self.hub_instance.web_ui.port}"
            qr = qrcode.make(url)
            buffer = io.BytesIO()
            qr.save(buffer, "PNG")
            pixmap = QPixmap()
            pixmap.loadFromData(buffer.getvalue())
            qr_label.setPixmap(pixmap)
            layout.addWidget(qr_label)
            dialog.exec()
        
        def disable_top_screen(self): self.hub_instance.screen_controller.disable_top_screen()
        def disable_bottom_screen(self): self.hub_instance.screen_controller.disable_bottom_screen()

        def start_color_profile_calibration(self):
            """Start color profile calibration mode."""
            if self.set_color_profile_button.isChecked():
                selected_color = self.color_profile_combo.currentText()
                self.color_profile_status_label.setText(f"Click on a {selected_color} ball in the video feed...")
                self.log_message(f"Waiting for user to click on a {selected_color} ball to set color profile.")
            else:
                self.color_profile_status_label.setText("Select a color profile and click 'Set Color Profile', then click on a ball in the video.")
                self.log_message("Color profile calibration cancelled.")

        def video_view_clicked(self, event):
            print(f"🖱️ video_view_clicked() called! Button checked: {self.set_color_profile_button.isChecked()}")
            # Ball management calibration removed - using legacy color tracking only
            if self.set_color_profile_button.isChecked():
                print(f"✅ Calibration mode is active!")
                scene_pos = self.video_view.mapToScene(event.pos())
                print(f"📍 Scene pos: {scene_pos}")
                pixmap_item = self.video_pixmap_item
                print(f"🖼️ Pixmap item exists: {pixmap_item is not None}")
                print(f"🖼️ Pixmap exists: {pixmap_item.pixmap() is not None if pixmap_item else False}")
                
                # Check if the click is within the pixmap bounds
                if pixmap_item.pixmap() and pixmap_item.sceneBoundingRect().contains(scene_pos):
                    print(f"✅ Click is within pixmap bounds!")
                    # Transform scene coordinates to pixmap (image) coordinates
                    img_pos = pixmap_item.mapFromScene(scene_pos)
                    print(f"📍 Image pos: ({img_pos.x():.1f}, {img_pos.y():.1f})")
                    
                    color_name = self.color_profile_combo.currentText()
                    print(f"🎨 Selected color: '{color_name}'")
                    self.log_message(f"Clicked at pixel ({img_pos.x():.1f}, {img_pos.y():.1f}) to set '{color_name}' color profile")
                    
                    # Get the current frame image
                    frame_image = self.get_latest_frame()
                    print(f"🖼️ Frame available: {frame_image is not None}")
                    
                    if frame_image is not None:
                        try:
                            print(f"✅ Frame is valid, proceeding with color sampling...")
                            # Extract color at the clicked position
                            x, y = int(img_pos.x()), int(img_pos.y())
                            # Ensure coordinates are within bounds
                            x = max(0, min(x, frame_image.shape[1] - 1))
                            y = max(0, min(y, frame_image.shape[0] - 1))
                            
                            # Sample a small region around the click for better color accuracy
                            sample_size = 10
                            x1 = max(0, x - sample_size)
                            y1 = max(0, y - sample_size)
                            x2 = min(frame_image.shape[1], x + sample_size)
                            y2 = min(frame_image.shape[0], y + sample_size)
                            
                            roi = frame_image[y1:y2, x1:x2]
                            hsv_roi = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
                            avg_hsv = np.mean(hsv_roi, axis=(0, 1))
                            
                            print(f"📊 Sampled HSV: H={avg_hsv[0]:.1f}, S={avg_hsv[1]:.1f}, V={avg_hsv[2]:.1f}")
                            self.log_message(f"Sampled HSV color: H={avg_hsv[0]:.1f}, S={avg_hsv[1]:.1f}, V={avg_hsv[2]:.1f}")
                            
                            print(f"📦 Creating ZMQ command...")
                            # Send calibration command to engine via ZMQ
                            command = juggler_pb2.CommandRequest()
                            print(f"📦 Command object created")
                            command.type = juggler_pb2.CommandRequest.CommandType.CALIBRATE_COLOR
                            print(f"📦 Command type set to CALIBRATE_COLOR")
                            command.color_name = color_name
                            print(f"📦 Color name set to '{color_name}'")
                            command.click_x = x
                            command.click_y = y
                            print(f"📦 Click coordinates set to ({x}, {y})")
                            
                            print(f"📤 About to send ZMQ command...")
                            self.log_message(f"📤 Sending CALIBRATE_COLOR command for '{color_name}' at ({x}, {y})")
                            response = self.zmq_client.send_command(command)
                            print(f"📥 Response received: success={response.success}, message='{response.message}'")
                            self.log_message(f"📥 Response from engine: success={response.success}, message='{response.message}'")
                            
                            if response.success:
                                print(f"✅ Response was successful, proceeding with reload...")
                                self.log_message(f"✅ Color profile '{color_name}' updated successfully from YOLO detection box")
                                self.color_profile_status_label.setText(f"✅ '{color_name}' profile set! Click 'Set Color Profile' again to calibrate another color.")
                                
                                # Reload ball profiles to update the hue sliders in Ball Profiles section
                                # CRITICAL FIX: Add small delay to allow engine to finish writing the file
                                # The engine saves the file after calibration, but we need to wait for it to complete
                                if hasattr(self, 'settings_widget') and self.settings_widget:
                                    self.log_message(f"🔄 Scheduling reload_ball_profiles() for '{color_name}' after 100ms delay")
                                    try:
                                        # Use QTimer to delay reload by 100ms to avoid race condition
                                        QTimer.singleShot(100, lambda: self._reload_ball_profiles_with_logging(color_name))
                                    except Exception as e:
                                        self.log_message(f"❌ Error scheduling reload: {e}")
                                        import traceback
                                        self.log_message(f"Stack trace: {traceback.format_exc()}")
                            else:
                                # Check if the error is about no YOLO box found
                                if "No YOLO detection box found" in response.message:
                                    self.log_message(f"❌ No ball detected at click location. Please click directly on a detected ball.")
                                    self.color_profile_status_label.setText(f"❌ No ball detected! Click on a ball with a YOLO detection box.")
                                    QMessageBox.warning(self, "No Ball Detected",
                                                       "No YOLO detection box found at the click location.\n\n"
                                                       "Please click directly on a detected ball (one with a red box around it).\n\n"
                                                       "Tip: Enable 'YOLO Detections' visualization to see where balls are detected.")
                                else:
                                    self.log_message(f"❌ Failed to update color profile: {response.message}")
                                    self.color_profile_status_label.setText(f"❌ Failed to set '{color_name}' profile: {response.message}")
                        except Exception as e:
                            print(f"❌ EXCEPTION during calibration: {e}")
                            import traceback
                            print(f"Stack trace: {traceback.format_exc()}")
                            self.log_message(f"❌ Error during calibration: {e}")
                            self.color_profile_status_label.setText(f"❌ Error: {e}")
                    else:
                        print(f"❌ ERROR: frame_image is None!")
                        self.log_message(f"❌ Error: No frame image available for color sampling")
                        self.color_profile_status_label.setText("❌ No frame available")
                    
                    # Uncheck the button after calibration
                    self.set_color_profile_button.setChecked(False)
                    self.color_profile_status_label.setText("Select a color profile and click 'Set Color Profile', then click on a ball in the video.")
            # Call original event handler
            QGraphicsView.mousePressEvent(self.video_view, event)
        
        def _reload_ball_profiles_with_logging(self, color_name: str):
            """Helper method to reload ball profiles after calibration with proper error handling"""
            try:
                self.log_message(f"🔄 _reload_ball_profiles_with_logging() called for '{color_name}'")
                
                if not hasattr(self, 'settings_widget'):
                    self.log_message(f"❌ ERROR: self.settings_widget does not exist!")
                    return
                
                if not self.settings_widget:
                    self.log_message(f"❌ ERROR: self.settings_widget is None!")
                    return
                
                if not hasattr(self.settings_widget, 'reload_ball_profiles'):
                    self.log_message(f"❌ ERROR: settings_widget does not have reload_ball_profiles method!")
                    return
                
                self.log_message(f"✅ Calling settings_widget.reload_ball_profiles()...")
                self.settings_widget.reload_ball_profiles()
                self.log_message(f"🎨 Ball profile hue sliders updated for '{color_name}'")
            except Exception as e:
                self.log_message(f"⚠️ Warning: Could not reload ball profiles: {e}")
                import traceback
                self.log_message(f"Stack trace: {traceback.format_exc()}")

class JuggleHubUI:
    def __init__(self, config: dict, zmq_client: Optional['ZMQClient'] = None, hub_instance=None):
        self.config = config
        self.zmq_client = zmq_client
        self.hub_instance = hub_instance
        if PYQT_AVAILABLE and config.get('enable_ui', True):
            self.app = QApplication.instance() or QApplication(sys.argv)
            self.main_window = JuggleHubMainWindow(config, self.zmq_client, self.hub_instance)
            self.ui_type = "pyqt6"
        else:
            self.console_ui = ConsoleUI(config)
            self.ui_type = "console"

    def get_latest_frame(self):
        """Returns the latest video frame as a numpy array."""
        if self.ui_type == "pyqt6" and hasattr(self.main_window, 'get_latest_frame'):
            return self.main_window.get_latest_frame()
        return None
    
    def update_frame_data(self, frame_data: juggler_pb2.FrameData):
        getattr(self.main_window if self.ui_type == "pyqt6" else self.console_ui, 'update_frame_data')(frame_data)
    
    def run(self):
        if self.ui_type == "pyqt6":
            print("🖥️ Starting PyQt6 UI...")
            self.main_window.show()
            self.app.exec()
        else:
            self.console_ui.run()
    
    def cleanup(self):
        print("🧹 Cleaning up UI...")
        if self.ui_type == "pyqt6":
            # Auto-save settings before closing
            if self.main_window and hasattr(self.main_window, 'settings_widget'):
                self.main_window.settings_widget.save_settings()
                print("💾 Auto-saved calibration settings on app close")
            if self.main_window: self.main_window.close()
            if self.app: self.app.quit()
        else:
            self.console_ui.cleanup()
        print("✅ UI cleanup completed")

if __name__ == "__main__":
    def test_ui():
        config = {'enable_ui': True, 'debug': True}
        ui = JuggleHubUI(config)
        def generate_test_data():
            time.sleep(2)
            for i in range(100):
                frame_data = juggler_pb2.FrameData(timestamp_us=int(time.time() * 1000000), frame_number=i+1)
                ball = frame_data.balls.add(id=i, position={'x': 0.1 * (i % 10 - 5), 'y': 0.1 * ((i // 10) % 10 - 5), 'z': 0.8 + 0.1 * (i % 5)})
                ui.update_frame_data(frame_data)
                time.sleep(0.033)
        threading.Thread(target=generate_test_data, daemon=True).start()
        ui.run()
    
    print("🧪 Testing JuggleHub UI...")
    test_ui()