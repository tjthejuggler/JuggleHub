"""
Custom Qt widgets for JuggleHub UI.
"""

try:
    from PyQt6.QtWidgets import QWidget, QVBoxLayout, QPushButton
    from PyQt6.QtCore import QObject, pyqtSignal, Qt
    PYQT_AVAILABLE = True
except ImportError:
    PYQT_AVAILABLE = False


if PYQT_AVAILABLE:
    class FrameDataSignal(QObject):
        """Signal emitter for thread-safe UI updates."""
        frame_received = pyqtSignal(object)

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