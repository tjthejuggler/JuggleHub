"""
Base App Class

Abstract base class for all JuggleHub apps.
"""

from abc import ABC, abstractmethod
from PyQt6.QtWidgets import QMainWindow
from typing import Optional, List
import juggler_pb2


class BaseApp(ABC):
    """Abstract base class for all JuggleHub apps."""
    
    def __init__(self, app_api: 'AppAPI'):
        """
        Initialize the base app.
        
        Args:
            app_api: AppAPI instance for communicating with the engine
        """
        self.api = app_api
        self.window: Optional[QMainWindow] = None
        self._running = False
    
    @abstractmethod
    def get_metadata(self) -> dict:
        """
        Return app metadata.
        
        Returns:
            dict: App metadata including id, name, version, description, etc.
        """
        pass
    
    @abstractmethod
    def initialize(self):
        """
        Initialize the app (called once on startup).
        
        Use this method to set up app state, load settings, etc.
        """
        pass
    
    @abstractmethod
    def create_window(self) -> QMainWindow:
        """
        Create and return the app's main window.
        
        Returns:
            QMainWindow: The app's main window
        """
        pass
    
    @abstractmethod
    def on_frame_data(self, frame_data: juggler_pb2.FrameData):
        """
        Handle incoming frame data from engine.
        
        Args:
            frame_data: FrameData message from the engine
        """
        pass
    
    def start(self):
        """Start the app."""
        if not self._running:
            print(f"🚀 Starting app: {self.get_metadata()['name']}")
            self.initialize()
            self.window = self.create_window()
            self._enable_required_features()
            self.api.subscribe_to_data(self.on_frame_data)
            self.window.show()
            self._running = True
            print(f"✅ App started: {self.get_metadata()['name']}")
    
    def stop(self):
        """Stop the app."""
        if self._running:
            print(f"🛑 Stopping app: {self.get_metadata()['name']}")
            self._disable_required_features()
            self.api.unsubscribe_from_data()
            if self.window:
                self.window.close()
            self._running = False
            print(f"✅ App stopped: {self.get_metadata()['name']}")
    
    def _enable_required_features(self):
        """Enable required engine features."""
        metadata = self.get_metadata()
        for feature in metadata.get('required_features', []):
            success = self.api.enable_feature(feature)
            if success:
                print(f"  ✅ Enabled feature: {feature}")
            else:
                print(f"  ⚠️ Failed to enable feature: {feature}")
    
    def _disable_required_features(self):
        """Disable required engine features."""
        metadata = self.get_metadata()
        for feature in metadata.get('required_features', []):
            success = self.api.disable_feature(feature)
            if success:
                print(f"  ✅ Disabled feature: {feature}")
            else:
                print(f"  ⚠️ Failed to disable feature: {feature}")