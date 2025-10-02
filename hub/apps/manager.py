"""
App Manager

Manages app lifecycle, registry, and window management.
"""

import json
import os
from typing import Dict, List, Optional
from datetime import datetime
import importlib
from .base import BaseApp
from .api import AppAPI


class AppManager:
    """Manages app lifecycle, registry, and window management."""
    
    def __init__(self, zmq_context):
        """
        Initialize the AppManager.
        
        Args:
            zmq_context: ZMQ context for creating app APIs
        """
        self.zmq_context = zmq_context
        
        # Determine the correct path to apps directory
        # Check if we're running from project root or hub directory
        if os.path.exists("hub/apps"):
            self.apps_dir = "hub/apps"
            self.registry_path = "hub/apps/registry.json"
        elif os.path.exists("apps"):
            self.apps_dir = "apps"
            self.registry_path = "apps/registry.json"
        else:
            # Fallback: use absolute path based on this file's location
            module_dir = os.path.dirname(os.path.abspath(__file__))
            self.apps_dir = module_dir
            self.registry_path = os.path.join(module_dir, "registry.json")
        
        self.registry = self._load_registry()
        self.running_apps: Dict[str, BaseApp] = {}
    
    def _load_registry(self) -> dict:
        """Load app registry from disk."""
        if os.path.exists(self.registry_path):
            try:
                with open(self.registry_path, 'r') as f:
                    return json.load(f)
            except Exception as e:
                print(f"⚠️ Error loading registry: {e}")
        
        # Return default registry
        return {
            "apps": [],
            "recent_apps": [],
            "max_recent": 5
        }
    
    def _save_registry(self):
        """Save app registry to disk."""
        try:
            os.makedirs(os.path.dirname(self.registry_path), exist_ok=True)
            with open(self.registry_path, 'w') as f:
                json.dump(self.registry, f, indent=2)
        except Exception as e:
            print(f"⚠️ Error saving registry: {e}")
    
    def discover_apps(self) -> List[dict]:
        """
        Discover all available apps.
        
        Returns:
            List[dict]: List of app metadata dictionaries
        """
        apps = []
        
        if not os.path.exists(self.apps_dir):
            print(f"⚠️ Apps directory not found: {self.apps_dir}")
            return apps
        
        for item in os.listdir(self.apps_dir):
            app_path = os.path.join(self.apps_dir, item)
            metadata_path = os.path.join(app_path, "metadata.json")
            
            # Skip if not a directory or no metadata file
            if not os.path.isdir(app_path) or not os.path.exists(metadata_path):
                continue
            
            try:
                with open(metadata_path, 'r') as f:
                    metadata = json.load(f)
                    metadata['path'] = app_path
                    apps.append(metadata)
            except Exception as e:
                print(f"⚠️ Error loading metadata for {item}: {e}")
        
        return apps
    
    def launch_app(self, app_id: str) -> Optional[BaseApp]:
        """
        Launch an app by ID.
        
        Args:
            app_id: ID of the app to launch
            
        Returns:
            BaseApp: The launched app instance, or None if failed
        """
        # Check if app is already running
        if app_id in self.running_apps:
            print(f"ℹ️ App '{app_id}' is already running, bringing to front")
            app = self.running_apps[app_id]
            if app.window:
                app.window.raise_()
                app.window.activateWindow()
            return app
        
        # Find app metadata
        apps = self.discover_apps()
        app_metadata = next((a for a in apps if a['id'] == app_id), None)
        
        if not app_metadata:
            print(f"❌ App '{app_id}' not found")
            return None
        
        # Import and instantiate app
        try:
            entry_point = app_metadata['entry_point']
            module_path, class_name = entry_point.rsplit(':', 1)
            
            # Import the module
            module = importlib.import_module(module_path)
            app_class = getattr(module, class_name)
            
            # Create app instance
            app_api = AppAPI(self.zmq_context)
            app = app_class(app_api)
            
            # Start app
            app.start()
            
            # Track running app
            self.running_apps[app_id] = app
            
            # Update recent apps
            self._update_recent_apps(app_id)
            
            print(f"✅ Launched app: {app_metadata['name']}")
            return app
            
        except Exception as e:
            print(f"❌ Failed to launch app '{app_id}': {e}")
            import traceback
            traceback.print_exc()
            return None
    
    def close_app(self, app_id: str):
        """
        Close a running app.
        
        Args:
            app_id: ID of the app to close
        """
        if app_id in self.running_apps:
            try:
                self.running_apps[app_id].stop()
                del self.running_apps[app_id]
                print(f"✅ Closed app: {app_id}")
            except Exception as e:
                print(f"❌ Error closing app '{app_id}': {e}")
    
    def close_all_apps(self):
        """Close all running apps."""
        app_ids = list(self.running_apps.keys())
        for app_id in app_ids:
            self.close_app(app_id)
    
    def _update_recent_apps(self, app_id: str):
        """
        Update recent apps list.
        
        Args:
            app_id: ID of the app to add to recent list
        """
        recent = self.registry.get('recent_apps', [])
        
        # Remove if already in list
        if app_id in recent:
            recent.remove(app_id)
        
        # Add to front
        recent.insert(0, app_id)
        
        # Trim to max size
        max_recent = self.registry.get('max_recent', 5)
        self.registry['recent_apps'] = recent[:max_recent]
        
        # Save registry
        self._save_registry()
    
    def get_recent_apps(self, limit: Optional[int] = None) -> List[str]:
        """
        Get list of recently used app IDs.
        
        Args:
            limit: Maximum number of recent apps to return (default: all)
        
        Returns:
            List[str]: List of app IDs in order of recent use
        """
        recent_ids = self.registry.get('recent_apps', [])
        
        if limit is not None:
            return recent_ids[:limit]
        return recent_ids
    
    def get_app_info(self, app_id: str) -> Optional[dict]:
        """
        Get metadata for a specific app.
        
        Args:
            app_id: ID of the app
            
        Returns:
            dict: App metadata, or None if not found
        """
        all_apps = self.discover_apps()
        return next((a for a in all_apps if a['id'] == app_id), None)
    
    def is_app_running(self, app_id: str) -> bool:
        """
        Check if an app is currently running.
        
        Args:
            app_id: ID of the app to check
            
        Returns:
            bool: True if app is running, False otherwise
        """
        return app_id in self.running_apps