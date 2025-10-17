"""
2D Tracker Settings Sections for JuggleHub UI.
Contains settings sections that are ONLY visible when 2D tracker is selected.
Currently minimal - 2D tracker uses only common settings.
"""


class Tracker2DSettingsSections:
    """2D tracker-specific settings sections (currently empty)."""
    
    def __init__(self, parent_widget, udp_client, zmq_client):
        """
        Initialize 2D tracker settings sections.
        
        Args:
            parent_widget: Parent CalibrationSettingsWidget instance
            udp_client: UDP client for sending settings to engine
            zmq_client: ZMQ client for sending commands to engine
        """
        self.parent = parent_widget
        self.udp_client = udp_client
        self.zmq_client = zmq_client
    
    # Currently, the 2D tracker uses only common settings (camera, YOLO, pose)
    # No 2D-specific settings sections are needed yet.
    # 
    # Future 2D-specific settings could include:
    # - 2D-specific tracking parameters
    # - Simplified ball state detection without depth
    # - 2D trajectory prediction settings
    # 
    # When adding 2D-specific sections, follow the same pattern as 3D sections:
    # def create_2d_specific_section(self):
    #     """Create a 2D-specific settings section"""
    #     section = CollapsibleGroupBox("Section Name", collapsed=False)
    #     layout = QGridLayout()
    #     section.get_content_layout().addLayout(layout)
    #     # Add widgets here
    #     return section