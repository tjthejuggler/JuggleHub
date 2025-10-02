"""
App Manager Dialog

Dialog for managing and launching JuggleHub apps.
"""

from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel,
                              QPushButton, QScrollArea, QWidget, QGridLayout)
from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QFont


class AppCard(QWidget):
    """Card widget for displaying app information."""
    
    def __init__(self, app_metadata: dict, manager, parent=None):
        super().__init__(parent)
        self.app_metadata = app_metadata
        self.manager = manager
        self._init_ui()
    
    def _init_ui(self):
        """Initialize the card UI."""
        layout = QVBoxLayout(self)
        layout.setSpacing(10)
        layout.setContentsMargins(15, 15, 15, 15)
        
        # Icon and title
        header = QHBoxLayout()
        icon_label = QLabel(self.app_metadata.get('icon', '📦'))
        icon_label.setFont(QFont("Arial", 32))
        header.addWidget(icon_label)
        
        title_label = QLabel(self.app_metadata['name'])
        title_label.setFont(QFont("Arial", 16, QFont.Weight.Bold))
        title_label.setWordWrap(True)
        header.addWidget(title_label, 1)
        layout.addLayout(header)
        
        # Description
        desc_label = QLabel(self.app_metadata.get('description', 'No description available'))
        desc_label.setWordWrap(True)
        desc_label.setStyleSheet("color: #aaaaaa;")
        desc_label.setMinimumHeight(60)
        layout.addWidget(desc_label)
        
        # Version and category
        info_layout = QHBoxLayout()
        version_label = QLabel(f"v{self.app_metadata.get('version', '1.0.0')}")
        version_label.setStyleSheet("color: #888888; font-size: 10px;")
        info_layout.addWidget(version_label)
        
        category_label = QLabel(f"• {self.app_metadata.get('category', 'general')}")
        category_label.setStyleSheet("color: #888888; font-size: 10px;")
        info_layout.addWidget(category_label)
        info_layout.addStretch()
        layout.addLayout(info_layout)
        
        # Launch button
        launch_btn = QPushButton("Launch")
        launch_btn.clicked.connect(self._launch_app)
        launch_btn.setFixedHeight(40)
        launch_btn.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                border: none;
                padding: 10px;
                border-radius: 5px;
                font-weight: bold;
                font-size: 14px;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
            QPushButton:pressed {
                background-color: #2e7d32;
            }
        """)
        layout.addWidget(launch_btn)
        
        # Card styling
        self.setStyleSheet("""
            AppCard {
                background-color: #3a3a3a;
                border: 2px solid #555555;
                border-radius: 10px;
            }
        """)
        self.setFixedSize(300, 240)
    
    def _launch_app(self):
        """Launch this app."""
        app_id = self.app_metadata['id']
        self.manager.launch_app(app_id)
        
        # Emit signal to parent dialog
        dialog = self.window()
        if isinstance(dialog, AppManagerDialog):
            dialog.app_launched.emit(app_id)
            dialog.close()


class AppManagerDialog(QDialog):
    """Dialog for managing and launching apps."""
    
    # Signal emitted when an app is launched
    app_launched = pyqtSignal(str)  # Emits app_id
    
    def __init__(self, app_manager, parent=None):
        super().__init__(parent)
        self.app_manager = app_manager
        self.setWindowTitle("App Manager")
        self.setGeometry(100, 100, 800, 600)
        self._init_ui()
    
    def _init_ui(self):
        """Initialize the dialog UI."""
        layout = QVBoxLayout(self)
        layout.setSpacing(20)
        layout.setContentsMargins(20, 20, 20, 20)
        
        # Title
        title = QLabel("🚀 JuggleHub Apps")
        title.setFont(QFont("Arial", 24, QFont.Weight.Bold))
        layout.addWidget(title)
        
        # Subtitle
        subtitle = QLabel("Launch apps to extend JuggleHub functionality")
        subtitle.setStyleSheet("color: #aaaaaa; font-size: 14px;")
        layout.addWidget(subtitle)
        
        # Scroll area for app cards
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.setStyleSheet("""
            QScrollArea {
                border: none;
                background-color: #2b2b2b;
            }
            QScrollBar:vertical {
                border: none;
                background: #1e1e1e;
                width: 12px;
                margin: 0px;
            }
            QScrollBar::handle:vertical {
                background: #555555;
                min-height: 20px;
                border-radius: 6px;
            }
            QScrollBar::handle:vertical:hover {
                background: #666666;
            }
        """)
        
        # Container for cards
        container = QWidget()
        grid = QGridLayout(container)
        grid.setSpacing(20)
        grid.setContentsMargins(10, 10, 10, 10)
        
        # Discover and display apps
        apps = self.app_manager.discover_apps()
        
        if not apps:
            # No apps found message
            no_apps_label = QLabel("No apps found. Apps should be placed in hub/apps/ directory.")
            no_apps_label.setStyleSheet("color: #aaaaaa; font-size: 14px;")
            no_apps_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            grid.addWidget(no_apps_label, 0, 0, 1, 2)
        else:
            # Display app cards in grid
            row, col = 0, 0
            max_cols = 2
            
            for app in apps:
                card = AppCard(app, self.app_manager, self)
                grid.addWidget(card, row, col)
                col += 1
                if col >= max_cols:
                    col = 0
                    row += 1
        
        scroll.setWidget(container)
        layout.addWidget(scroll, 1)
        
        # Close button
        close_btn = QPushButton("Close")
        close_btn.clicked.connect(self.close)
        close_btn.setFixedHeight(40)
        close_btn.setStyleSheet("""
            QPushButton {
                background-color: #555555;
                color: white;
                border: none;
                padding: 10px;
                border-radius: 5px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #666666;
            }
        """)
        layout.addWidget(close_btn)
        
        # Apply dark theme
        self.setStyleSheet("""
            QDialog {
                background-color: #2b2b2b;
                color: #ffffff;
            }
        """)