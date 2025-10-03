"""
Ball Management Widget

Provides a user-friendly UI for managing balls in the Hub.
This widget allows users to create, calibrate, activate, and manage balls
for the new ball tracking system.

Created: 2025-10-03
"""

import logging
from typing import Optional, Dict, List
from datetime import datetime

try:
    from PyQt6.QtWidgets import (
        QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
        QListWidget, QListWidgetItem, QGroupBox, QComboBox,
        QMessageBox, QInputDialog, QTextEdit, QSplitter
    )
    from PyQt6.QtCore import Qt, pyqtSignal
    from PyQt6.QtGui import QFont, QColor
    PYQT_AVAILABLE = True
except ImportError:
    PYQT_AVAILABLE = False
    print("⚠️ PyQt6 not available for ball management widget")

logger = logging.getLogger(__name__)


class BallManagementWidget(QWidget):
    """
    Widget for managing balls in the Hub.
    
    Features:
    - Create new balls with custom names
    - View all registered balls
    - Activate/deactivate balls for tracking
    - Add color calibration samples
    - Remove individual samples
    - Delete balls
    - Switch between legacy and new tracking modes
    """
    
    # Signals
    calibration_requested = pyqtSignal(str)  # Emits ball_id when calibration is requested
    
    def __init__(self, ball_manager, parent=None):
        """
        Initialize the ball management widget.
        
        Args:
            ball_manager: BallManager instance for backend operations
            parent: Parent widget
        """
        super().__init__(parent)
        self.ball_manager = ball_manager
        self.selected_ball_id = None
        self.calibration_mode = False
        self.calibrating_ball_id = None
        
        self.init_ui()
        self.refresh_ball_list()
        
        logger.info("BallManagementWidget initialized")
    
    def init_ui(self):
        """Initialize the user interface."""
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(5, 5, 5, 5)
        
        # Title
        title = QLabel("🏀 Ball Management")
        title.setFont(QFont("Arial", 14, QFont.Weight.Bold))
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        main_layout.addWidget(title)
        
        # Tracking mode toggle
        mode_group = QGroupBox("Tracking Mode")
        mode_layout = QVBoxLayout(mode_group)
        
        self.mode_status_label = QLabel("Current Mode: Loading...")
        mode_layout.addWidget(self.mode_status_label)
        
        mode_buttons_layout = QHBoxLayout()
        self.legacy_mode_button = QPushButton("Legacy Tracking")
        self.legacy_mode_button.setCheckable(True)
        self.legacy_mode_button.clicked.connect(lambda: self.switch_tracking_mode(False))
        mode_buttons_layout.addWidget(self.legacy_mode_button)
        
        self.new_mode_button = QPushButton("New Ball Tracking")
        self.new_mode_button.setCheckable(True)
        self.new_mode_button.clicked.connect(lambda: self.switch_tracking_mode(True))
        mode_buttons_layout.addWidget(self.new_mode_button)
        
        mode_layout.addLayout(mode_buttons_layout)
        main_layout.addWidget(mode_group)
        
        # Create splitter for resizable panels
        splitter = QSplitter(Qt.Orientation.Vertical)
        
        # Ball list panel
        list_group = QGroupBox("Registered Balls")
        list_layout = QVBoxLayout(list_group)
        
        # Ball list
        self.ball_list = QListWidget()
        self.ball_list.itemClicked.connect(self.on_ball_selected)
        list_layout.addWidget(self.ball_list)
        
        # Ball list buttons
        list_buttons_layout = QHBoxLayout()
        
        self.create_button = QPushButton("➕ Create Ball")
        self.create_button.clicked.connect(self.create_ball)
        self.create_button.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                padding: 8px;
                border-radius: 4px;
                font-weight: bold;
            }
            QPushButton:hover { background-color: #45a049; }
        """)
        list_buttons_layout.addWidget(self.create_button)
        
        self.delete_button = QPushButton("🗑️ Delete")
        self.delete_button.clicked.connect(self.delete_ball)
        self.delete_button.setEnabled(False)
        self.delete_button.setStyleSheet("""
            QPushButton {
                background-color: #f44336;
                color: white;
                padding: 8px;
                border-radius: 4px;
                font-weight: bold;
            }
            QPushButton:hover { background-color: #da190b; }
            QPushButton:disabled { background-color: #666666; }
        """)
        list_buttons_layout.addWidget(self.delete_button)
        
        self.activate_button = QPushButton("✓ Activate")
        self.activate_button.clicked.connect(self.activate_ball)
        self.activate_button.setEnabled(False)
        list_buttons_layout.addWidget(self.activate_button)
        
        self.deactivate_button = QPushButton("✗ Deactivate")
        self.deactivate_button.clicked.connect(self.deactivate_ball)
        self.deactivate_button.setEnabled(False)
        list_buttons_layout.addWidget(self.deactivate_button)
        
        list_layout.addLayout(list_buttons_layout)
        
        # Refresh button
        self.refresh_button = QPushButton("🔄 Refresh List")
        self.refresh_button.clicked.connect(self.refresh_ball_list)
        list_layout.addWidget(self.refresh_button)
        
        splitter.addWidget(list_group)
        
        # Ball details panel
        details_group = QGroupBox("Ball Details")
        details_layout = QVBoxLayout(details_group)
        
        self.details_text = QTextEdit()
        self.details_text.setReadOnly(True)
        self.details_text.setMaximumHeight(150)
        details_layout.addWidget(self.details_text)
        
        splitter.addWidget(details_group)
        
        # Calibration panel
        calibration_group = QGroupBox("Color Calibration")
        calibration_layout = QVBoxLayout(calibration_group)
        
        # Instructions
        instructions = QLabel(
            "📝 Instructions:\n"
            "1. Select a ball from the list above\n"
            "2. Choose lighting condition\n"
            "3. Click 'Add Sample' button\n"
            "4. Click on the ball in the video feed\n"
            "5. Repeat for different lighting conditions (3-5 samples recommended)"
        )
        instructions.setWordWrap(True)
        instructions.setStyleSheet("color: #aaaaaa; font-size: 10px;")
        calibration_layout.addWidget(instructions)
        
        # Lighting condition selector
        lighting_layout = QHBoxLayout()
        lighting_layout.addWidget(QLabel("Lighting Condition:"))
        self.lighting_combo = QComboBox()
        self.lighting_combo.addItems(["Bright", "Dim", "Mixed", "Custom"])
        lighting_layout.addWidget(self.lighting_combo)
        calibration_layout.addLayout(lighting_layout)
        
        # Sample counter
        self.sample_counter_label = QLabel("Samples: 0/5 recommended")
        self.sample_counter_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.sample_counter_label.setStyleSheet("font-weight: bold; color: #FFA500;")
        calibration_layout.addWidget(self.sample_counter_label)
        
        # Calibration buttons
        cal_buttons_layout = QHBoxLayout()
        
        self.add_sample_button = QPushButton("📸 Add Sample")
        self.add_sample_button.clicked.connect(self.start_calibration)
        self.add_sample_button.setEnabled(False)
        self.add_sample_button.setStyleSheet("""
            QPushButton {
                background-color: #2196F3;
                color: white;
                padding: 10px;
                border-radius: 4px;
                font-weight: bold;
            }
            QPushButton:hover { background-color: #0b7dda; }
            QPushButton:disabled { background-color: #666666; }
        """)
        cal_buttons_layout.addWidget(self.add_sample_button)
        
        self.remove_sample_button = QPushButton("🗑️ Remove Sample")
        self.remove_sample_button.clicked.connect(self.remove_sample)
        self.remove_sample_button.setEnabled(False)
        cal_buttons_layout.addWidget(self.remove_sample_button)
        
        calibration_layout.addLayout(cal_buttons_layout)
        
        # Calibration status
        self.calibration_status_label = QLabel("Ready")
        self.calibration_status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.calibration_status_label.setStyleSheet("color: #4CAF50; font-weight: bold;")
        calibration_layout.addWidget(self.calibration_status_label)
        
        splitter.addWidget(calibration_group)
        
        # Set splitter sizes (proportions)
        splitter.setSizes([300, 150, 250])
        
        main_layout.addWidget(splitter)
        
        # Update tracking mode status
        self.update_tracking_mode_status()
    
    def update_tracking_mode_status(self):
        """Update the tracking mode status display."""
        try:
            is_new = self.ball_manager.is_using_new_system()
            self.mode_status_label.setText(
                f"Current Mode: {'🆕 New Ball Tracking' if is_new else '📜 Legacy Tracking'}"
            )
            self.new_mode_button.setChecked(is_new)
            self.legacy_mode_button.setChecked(not is_new)
            
            # Style the checked button
            if is_new:
                self.new_mode_button.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
                self.legacy_mode_button.setStyleSheet("")
            else:
                self.legacy_mode_button.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
                self.new_mode_button.setStyleSheet("")
        except Exception as e:
            logger.error(f"Error updating tracking mode status: {e}")
            self.mode_status_label.setText("Current Mode: Error")
    
    def switch_tracking_mode(self, use_new: bool):
        """Switch between legacy and new tracking modes."""
        try:
            mode_name = "New Ball Tracking" if use_new else "Legacy Tracking"
            
            # Show warning dialog
            reply = QMessageBox.question(
                self,
                "Switch Tracking Mode",
                f"Are you sure you want to switch to {mode_name}?\n\n"
                "This will affect how balls are tracked in the system.",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No
            )
            
            if reply == QMessageBox.StandardButton.Yes:
                self.ball_manager.set_use_new_system(use_new)
                self.update_tracking_mode_status()
                QMessageBox.information(
                    self,
                    "Mode Switched",
                    f"Successfully switched to {mode_name}"
                )
                logger.info(f"Switched to {mode_name}")
            else:
                # Revert button state
                self.update_tracking_mode_status()
        except Exception as e:
            logger.error(f"Error switching tracking mode: {e}")
            QMessageBox.critical(self, "Error", f"Failed to switch mode:\n{str(e)}")
            self.update_tracking_mode_status()
    
    def refresh_ball_list(self):
        """Refresh the list of balls from the backend."""
        try:
            self.ball_list.clear()
            balls = self.ball_manager.get_all_balls()
            
            for ball in balls:
                ball_id = ball.get('id', 'unknown')
                display_name = ball.get('display_name', ball_id)
                is_active = ball.get('is_active', False)
                sample_count = ball.get('sample_count', 0)
                
                # Create list item
                status_icon = "✓" if is_active else "○"
                item_text = f"{status_icon} {display_name} ({sample_count} samples)"
                
                item = QListWidgetItem(item_text)
                item.setData(Qt.ItemDataRole.UserRole, ball_id)
                
                # Color code by status
                if is_active:
                    item.setForeground(QColor(76, 175, 80))  # Green
                else:
                    item.setForeground(QColor(158, 158, 158))  # Gray
                
                self.ball_list.addItem(item)
            
            logger.info(f"Refreshed ball list: {len(balls)} balls")
            
            # Update tracking mode status
            self.update_tracking_mode_status()
            
        except Exception as e:
            logger.error(f"Error refreshing ball list: {e}")
            QMessageBox.critical(self, "Error", f"Failed to refresh ball list:\n{str(e)}")
    
    def on_ball_selected(self, item: QListWidgetItem):
        """Handle ball selection from the list."""
        self.selected_ball_id = item.data(Qt.ItemDataRole.UserRole)
        
        # Enable/disable buttons based on selection
        self.delete_button.setEnabled(True)
        self.add_sample_button.setEnabled(True)
        
        # Get ball details
        try:
            balls = self.ball_manager.get_all_balls()
            ball = next((b for b in balls if b.get('id') == self.selected_ball_id), None)
            
            if ball:
                is_active = ball.get('is_active', False)
                self.activate_button.setEnabled(not is_active)
                self.deactivate_button.setEnabled(is_active)
                
                # Update details panel
                self.update_ball_details(ball)
            
        except Exception as e:
            logger.error(f"Error getting ball details: {e}")
    
    def update_ball_details(self, ball: Dict):
        """Update the ball details panel."""
        ball_id = ball.get('id', 'unknown')
        display_name = ball.get('display_name', ball_id)
        is_active = ball.get('is_active', False)
        samples = ball.get('color_samples', [])
        hsv_ranges = ball.get('hsv_ranges', {})
        
        details = f"<h3>{display_name}</h3>"
        details += f"<p><b>ID:</b> {ball_id}</p>"
        details += f"<p><b>Status:</b> {'🟢 Active' if is_active else '⚪ Inactive'}</p>"
        details += f"<p><b>Color Samples:</b> {len(samples)}</p>"
        
        if samples:
            details += "<p><b>Sample Details:</b></p><ul>"
            for i, sample in enumerate(samples):
                timestamp = sample.get('timestamp', 'unknown')
                lighting = sample.get('lighting', 'unknown')
                details += f"<li>Sample {i+1}: {lighting} lighting ({timestamp})</li>"
            details += "</ul>"
        
        if hsv_ranges:
            details += f"<p><b>HSV Ranges:</b></p>"
            details += f"<p>H: [{hsv_ranges.get('h_min', 0):.1f}, {hsv_ranges.get('h_max', 180):.1f}]</p>"
            details += f"<p>S: [{hsv_ranges.get('s_min', 0):.1f}, {hsv_ranges.get('s_max', 255):.1f}]</p>"
            details += f"<p>V: [{hsv_ranges.get('v_min', 0):.1f}, {hsv_ranges.get('v_max', 255):.1f}]</p>"
        
        self.details_text.setHtml(details)
        
        # Update sample counter
        sample_count = len(samples)
        if sample_count >= 5:
            color = "#4CAF50"  # Green
            status = "✓"
        elif sample_count >= 3:
            color = "#FFA500"  # Orange
            status = "⚠"
        else:
            color = "#f44336"  # Red
            status = "!"
        
        self.sample_counter_label.setText(f"{status} Samples: {sample_count}/5 recommended")
        self.sample_counter_label.setStyleSheet(f"font-weight: bold; color: {color};")
        
        # Enable remove sample button if there are samples
        self.remove_sample_button.setEnabled(sample_count > 0)
    
    def create_ball(self):
        """Create a new ball."""
        name, ok = QInputDialog.getText(
            self,
            "Create Ball",
            "Enter ball name:",
            text="My Ball"
        )
        
        if ok and name:
            try:
                ball_id = self.ball_manager.create_ball(name)
                QMessageBox.information(
                    self,
                    "Success",
                    f"Ball '{name}' created successfully!\nID: {ball_id}"
                )
                self.refresh_ball_list()
                logger.info(f"Created ball: {name} ({ball_id})")
            except Exception as e:
                logger.error(f"Error creating ball: {e}")
                QMessageBox.critical(self, "Error", f"Failed to create ball:\n{str(e)}")
    
    def delete_ball(self):
        """Delete the selected ball."""
        if not self.selected_ball_id:
            return
        
        reply = QMessageBox.question(
            self,
            "Delete Ball",
            f"Are you sure you want to delete ball '{self.selected_ball_id}'?\n\n"
            "This action cannot be undone.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            try:
                self.ball_manager.delete_ball(self.selected_ball_id)
                QMessageBox.information(self, "Success", "Ball deleted successfully")
                self.selected_ball_id = None
                self.refresh_ball_list()
                self.details_text.clear()
                logger.info(f"Deleted ball: {self.selected_ball_id}")
            except Exception as e:
                logger.error(f"Error deleting ball: {e}")
                QMessageBox.critical(self, "Error", f"Failed to delete ball:\n{str(e)}")
    
    def activate_ball(self):
        """Activate the selected ball for tracking."""
        if not self.selected_ball_id:
            return
        
        try:
            self.ball_manager.activate_ball(self.selected_ball_id)
            QMessageBox.information(self, "Success", "Ball activated for tracking")
            self.refresh_ball_list()
            logger.info(f"Activated ball: {self.selected_ball_id}")
        except Exception as e:
            logger.error(f"Error activating ball: {e}")
            QMessageBox.critical(self, "Error", f"Failed to activate ball:\n{str(e)}")
    
    def deactivate_ball(self):
        """Deactivate the selected ball."""
        if not self.selected_ball_id:
            return
        
        try:
            self.ball_manager.deactivate_ball(self.selected_ball_id)
            QMessageBox.information(self, "Success", "Ball deactivated")
            self.refresh_ball_list()
            logger.info(f"Deactivated ball: {self.selected_ball_id}")
        except Exception as e:
            logger.error(f"Error deactivating ball: {e}")
            QMessageBox.critical(self, "Error", f"Failed to deactivate ball:\n{str(e)}")
    
    def start_calibration(self):
        """Start color calibration mode."""
        if not self.selected_ball_id:
            QMessageBox.warning(self, "Warning", "Please select a ball first")
            return
        
        self.calibration_mode = True
        self.calibrating_ball_id = self.selected_ball_id
        self.calibration_status_label.setText("⏳ Waiting for click on video feed...")
        self.calibration_status_label.setStyleSheet("color: #FFA500; font-weight: bold;")
        
        # Emit signal to notify main window
        self.calibration_requested.emit(self.selected_ball_id)
        
        logger.info(f"Started calibration for ball: {self.selected_ball_id}")
    
    def on_calibration_click(self, x: int, y: int):
        """Handle calibration click from video feed."""
        if not self.calibration_mode or not self.calibrating_ball_id:
            return
        
        lighting = self.lighting_combo.currentText().lower()
        
        try:
            self.ball_manager.add_color_sample(
                self.calibrating_ball_id,
                x, y,
                lighting
            )
            
            self.calibration_status_label.setText("✓ Sample added successfully!")
            self.calibration_status_label.setStyleSheet("color: #4CAF50; font-weight: bold;")
            
            # Refresh to show new sample
            self.refresh_ball_list()
            
            # Find and select the ball again to update details
            for i in range(self.ball_list.count()):
                item = self.ball_list.item(i)
                if item.data(Qt.ItemDataRole.UserRole) == self.calibrating_ball_id:
                    self.ball_list.setCurrentItem(item)
                    self.on_ball_selected(item)
                    break
            
            logger.info(f"Added color sample to ball {self.calibrating_ball_id} at ({x}, {y})")
            
        except Exception as e:
            logger.error(f"Error adding color sample: {e}")
            self.calibration_status_label.setText(f"✗ Error: {str(e)}")
            self.calibration_status_label.setStyleSheet("color: #f44336; font-weight: bold;")
        
        finally:
            self.calibration_mode = False
            self.calibrating_ball_id = None
    
    def remove_sample(self):
        """Remove a color sample from the selected ball."""
        if not self.selected_ball_id:
            return
        
        # Get ball details to show sample list
        try:
            balls = self.ball_manager.get_all_balls()
            ball = next((b for b in balls if b.get('id') == self.selected_ball_id), None)
            
            if not ball or not ball.get('color_samples'):
                QMessageBox.warning(self, "Warning", "No samples to remove")
                return
            
            samples = ball.get('color_samples', [])
            sample_list = [f"Sample {i+1}: {s.get('lighting', 'unknown')} ({s.get('timestamp', 'unknown')})" 
                          for i, s in enumerate(samples)]
            
            item, ok = QInputDialog.getItem(
                self,
                "Remove Sample",
                "Select sample to remove:",
                sample_list,
                0,
                False
            )
            
            if ok and item:
                sample_index = sample_list.index(item)
                self.ball_manager.remove_color_sample(self.selected_ball_id, sample_index)
                QMessageBox.information(self, "Success", "Sample removed successfully")
                self.refresh_ball_list()
                
                # Reselect ball to update details
                for i in range(self.ball_list.count()):
                    list_item = self.ball_list.item(i)
                    if list_item.data(Qt.ItemDataRole.UserRole) == self.selected_ball_id:
                        self.ball_list.setCurrentItem(list_item)
                        self.on_ball_selected(list_item)
                        break
                
                logger.info(f"Removed sample {sample_index} from ball {self.selected_ball_id}")
                
        except Exception as e:
            logger.error(f"Error removing sample: {e}")
            QMessageBox.critical(self, "Error", f"Failed to remove sample:\n{str(e)}")
    
    def is_in_calibration_mode(self) -> bool:
        """Check if widget is currently in calibration mode."""
        return self.calibration_mode
    
    def get_calibrating_ball_id(self) -> Optional[str]:
        """Get the ID of the ball currently being calibrated."""
        return self.calibrating_ball_id if self.calibration_mode else None