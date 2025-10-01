"""
Color Profile Manager

Provides a dialog for managing color profiles dynamically.
"""

import json
import os
from pathlib import Path
from datetime import datetime

try:
    from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QTableWidget, 
                                 QTableWidgetItem, QPushButton, QLabel, QLineEdit,
                                 QColorDialog, QMessageBox, QHeaderView, QWidget)
    from PyQt6.QtCore import Qt
    from PyQt6.QtGui import QColor
    PYQT_AVAILABLE = True
except ImportError:
    PYQT_AVAILABLE = False


class ColorProfileManager:
    """Manages color profiles configuration."""
    
    def __init__(self, config_path='hub/config/color_profiles.json'):
        self.config_path = Path(config_path)
        self.profiles = []
        self.load_profiles()
    
    def load_profiles(self):
        """Load color profiles from JSON file."""
        if not self.config_path.exists():
            # Create default profiles if file doesn't exist
            self.profiles = self._get_default_profiles()
            self.save_profiles()
            return
        
        try:
            with open(self.config_path, 'r') as f:
                data = json.load(f)
                self.profiles = data.get('profiles', [])
        except Exception as e:
            print(f"❌ Error loading color profiles: {e}")
            self.profiles = self._get_default_profiles()
    
    def save_profiles(self):
        """Save color profiles to JSON file."""
        self.config_path.parent.mkdir(parents=True, exist_ok=True)
        
        data = {
            'profiles': self.profiles,
            'last_updated': datetime.now().isoformat()
        }
        
        try:
            with open(self.config_path, 'w') as f:
                json.dump(data, f, indent=2)
            print(f"✅ Color profiles saved to {self.config_path}")
            return True
        except Exception as e:
            print(f"❌ Error saving color profiles: {e}")
            return False
    
    def get_enabled_profiles(self):
        """Get list of enabled color profiles."""
        return [p for p in self.profiles if p.get('enabled', True)]
    
    def get_profile_names(self):
        """Get list of enabled profile names."""
        return [p['name'] for p in self.get_enabled_profiles()]
    
    def get_profile_display_names(self):
        """Get list of enabled profile display names."""
        return [p.get('display_name', p['name']) for p in self.get_enabled_profiles()]
    
    def get_color_map(self):
        """Get mapping of profile names to QColor objects."""
        color_map = {}
        for profile in self.get_enabled_profiles():
            rgb = profile.get('rgb', [255, 255, 255])
            color_map[profile['name']] = QColor(rgb[0], rgb[1], rgb[2]) if PYQT_AVAILABLE else rgb
        return color_map
    
    def add_profile(self, name, display_name, rgb, enabled=True):
        """Add a new color profile."""
        # Check if profile already exists
        for profile in self.profiles:
            if profile['name'].lower() == name.lower():
                return False, "Profile with this name already exists"
        
        self.profiles.append({
            'name': name.lower(),
            'display_name': display_name,
            'rgb': rgb,
            'enabled': enabled
        })
        return True, "Profile added successfully"
    
    def update_profile(self, index, name, display_name, rgb, enabled):
        """Update an existing color profile."""
        if 0 <= index < len(self.profiles):
            self.profiles[index] = {
                'name': name.lower(),
                'display_name': display_name,
                'rgb': rgb,
                'enabled': enabled
            }
            return True, "Profile updated successfully"
        return False, "Invalid profile index"
    
    def delete_profile(self, index):
        """Delete a color profile."""
        if 0 <= index < len(self.profiles):
            self.profiles.pop(index)
            return True, "Profile deleted successfully"
        return False, "Invalid profile index"
    
    def _get_default_profiles(self):
        """Get default color profiles."""
        return [
            {'name': 'pink', 'display_name': 'Pink', 'rgb': [233, 30, 99], 'enabled': True},
            {'name': 'orange', 'display_name': 'Orange', 'rgb': [255, 87, 34], 'enabled': True},
            {'name': 'yellow', 'display_name': 'Yellow', 'rgb': [255, 235, 59], 'enabled': True},
            {'name': 'green', 'display_name': 'Green', 'rgb': [76, 175, 80], 'enabled': True},
            {'name': 'red', 'display_name': 'Red', 'rgb': [244, 67, 54], 'enabled': True},
            {'name': 'blue', 'display_name': 'Blue', 'rgb': [33, 150, 243], 'enabled': True},
            {'name': 'purple', 'display_name': 'Purple', 'rgb': [156, 39, 176], 'enabled': True},
            {'name': 'white', 'display_name': 'White', 'rgb': [255, 255, 255], 'enabled': True},
        ]


if PYQT_AVAILABLE:
    class ColorProfileDialog(QDialog):
        """Dialog for managing color profiles."""
        
        def __init__(self, parent=None):
            super().__init__(parent)
            self.manager = ColorProfileManager()
            self.init_ui()
            self.load_table()
        
        def init_ui(self):
            """Initialize the user interface."""
            self.setWindowTitle("Color Profile Manager")
            self.setMinimumSize(600, 400)
            
            layout = QVBoxLayout(self)
            
            # Instructions
            info_label = QLabel("Manage color profiles for ball identification. Add, edit, or remove color profiles below.")
            info_label.setWordWrap(True)
            layout.addWidget(info_label)
            
            # Table
            self.table = QTableWidget()
            self.table.setColumnCount(5)
            self.table.setHorizontalHeaderLabels(['Name', 'Display Name', 'Color', 'Enabled', 'Actions'])
            self.table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
            self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
            layout.addWidget(self.table)
            
            # Add new profile section
            add_layout = QHBoxLayout()
            add_layout.addWidget(QLabel("Add New Profile:"))
            
            self.name_input = QLineEdit()
            self.name_input.setPlaceholderText("Name (e.g., white)")
            add_layout.addWidget(self.name_input)
            
            self.display_name_input = QLineEdit()
            self.display_name_input.setPlaceholderText("Display Name (e.g., White)")
            add_layout.addWidget(self.display_name_input)
            
            self.color_button = QPushButton("Choose Color")
            self.color_button.clicked.connect(self.choose_color)
            self.selected_color = QColor(255, 255, 255)
            self.update_color_button()
            add_layout.addWidget(self.color_button)
            
            add_button = QPushButton("Add Profile")
            add_button.clicked.connect(self.add_profile)
            add_layout.addWidget(add_button)
            
            layout.addLayout(add_layout)
            
            # Bottom buttons
            button_layout = QHBoxLayout()
            button_layout.addStretch()
            
            save_button = QPushButton("Save & Close")
            save_button.clicked.connect(self.save_and_close)
            button_layout.addWidget(save_button)
            
            cancel_button = QPushButton("Cancel")
            cancel_button.clicked.connect(self.reject)
            button_layout.addWidget(cancel_button)
            
            layout.addLayout(button_layout)
        
        def load_table(self):
            """Load profiles into the table."""
            self.table.setRowCount(len(self.manager.profiles))
            
            for i, profile in enumerate(self.manager.profiles):
                # Name
                self.table.setItem(i, 0, QTableWidgetItem(profile['name']))
                
                # Display Name
                self.table.setItem(i, 1, QTableWidgetItem(profile.get('display_name', profile['name'])))
                
                # Color preview
                color_widget = QWidget()
                color_layout = QHBoxLayout(color_widget)
                color_layout.setContentsMargins(5, 5, 5, 5)
                
                rgb = profile.get('rgb', [255, 255, 255])
                color_preview = QPushButton()
                color_preview.setFixedSize(50, 25)
                color_preview.setStyleSheet(f"background-color: rgb({rgb[0]}, {rgb[1]}, {rgb[2]}); border: 1px solid #555;")
                color_preview.clicked.connect(lambda checked, idx=i: self.edit_color(idx))
                color_layout.addWidget(color_preview)
                color_layout.addStretch()
                
                self.table.setCellWidget(i, 2, color_widget)
                
                # Enabled checkbox
                enabled_item = QTableWidgetItem()
                enabled_item.setFlags(Qt.ItemFlag.ItemIsUserCheckable | Qt.ItemFlag.ItemIsEnabled)
                enabled_item.setCheckState(Qt.CheckState.Checked if profile.get('enabled', True) else Qt.CheckState.Unchecked)
                self.table.setItem(i, 3, enabled_item)
                
                # Actions
                actions_widget = QWidget()
                actions_layout = QHBoxLayout(actions_widget)
                actions_layout.setContentsMargins(5, 5, 5, 5)
                
                delete_button = QPushButton("Delete")
                delete_button.clicked.connect(lambda checked, idx=i: self.delete_profile(idx))
                delete_button.setStyleSheet("background-color: #f44336; color: white;")
                actions_layout.addWidget(delete_button)
                actions_layout.addStretch()
                
                self.table.setCellWidget(i, 4, actions_widget)
        
        def choose_color(self):
            """Open color picker dialog."""
            color = QColorDialog.getColor(self.selected_color, self, "Choose Color")
            if color.isValid():
                self.selected_color = color
                self.update_color_button()
        
        def update_color_button(self):
            """Update the color button appearance."""
            self.color_button.setStyleSheet(
                f"background-color: {self.selected_color.name()}; "
                f"color: {'black' if self.selected_color.lightness() > 128 else 'white'};"
            )
        
        def add_profile(self):
            """Add a new profile."""
            name = self.name_input.text().strip()
            display_name = self.display_name_input.text().strip()
            
            if not name:
                QMessageBox.warning(self, "Invalid Input", "Please enter a profile name.")
                return
            
            if not display_name:
                display_name = name.capitalize()
            
            rgb = [self.selected_color.red(), self.selected_color.green(), self.selected_color.blue()]
            
            success, message = self.manager.add_profile(name, display_name, rgb)
            
            if success:
                self.load_table()
                self.name_input.clear()
                self.display_name_input.clear()
                self.selected_color = QColor(255, 255, 255)
                self.update_color_button()
                QMessageBox.information(self, "Success", message)
            else:
                QMessageBox.warning(self, "Error", message)
        
        def edit_color(self, index):
            """Edit the color of a profile."""
            if 0 <= index < len(self.manager.profiles):
                profile = self.manager.profiles[index]
                rgb = profile.get('rgb', [255, 255, 255])
                current_color = QColor(rgb[0], rgb[1], rgb[2])
                
                color = QColorDialog.getColor(current_color, self, "Choose Color")
                if color.isValid():
                    profile['rgb'] = [color.red(), color.green(), color.blue()]
                    self.load_table()
        
        def delete_profile(self, index):
            """Delete a profile."""
            reply = QMessageBox.question(
                self, 
                "Confirm Delete", 
                "Are you sure you want to delete this color profile?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
            )
            
            if reply == QMessageBox.StandardButton.Yes:
                success, message = self.manager.delete_profile(index)
                if success:
                    self.load_table()
                else:
                    QMessageBox.warning(self, "Error", message)
        
        def save_and_close(self):
            """Save profiles and close dialog."""
            # Update enabled status from checkboxes
            for i in range(self.table.rowCount()):
                enabled_item = self.table.item(i, 3)
                if enabled_item and i < len(self.manager.profiles):
                    self.manager.profiles[i]['enabled'] = (enabled_item.checkState() == Qt.CheckState.Checked)
            
            if self.manager.save_profiles():
                self.accept()
            else:
                QMessageBox.critical(self, "Error", "Failed to save color profiles.")