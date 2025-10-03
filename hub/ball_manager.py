#!/usr/bin/env python3
"""
Ball Manager - Python wrapper for engine BallRegistry operations

This module provides a Python interface to manage balls through the C++ engine's
BallRegistry via ZMQ communication. It handles ball creation, deletion, activation,
color calibration, and tracking mode management.

Created: 2025-10-03
"""

import logging
import json
from typing import List, Dict, Optional, Tuple
import juggler_pb2
from components.zmq_client import ZMQClient

logger = logging.getLogger(__name__)


class BallManager:
    """
    Manages ball operations by communicating with the C++ engine's BallRegistry.
    
    This class provides a high-level Python API for ball management operations
    that are executed by the C++ engine via ZMQ request-response protocol.
    """
    
    def __init__(self, zmq_client: Optional[ZMQClient] = None):
        """
        Initialize the BallManager.
        
        Args:
            zmq_client: Optional ZMQClient instance. If None, creates a new one.
        """
        self.zmq_client = zmq_client or ZMQClient()
        logger.info("BallManager initialized")
    
    def _send_command(self, command: juggler_pb2.CommandRequest) -> juggler_pb2.CommandResponse:
        """
        Send a command to the engine and return the response.
        
        Args:
            command: CommandRequest protobuf message
            
        Returns:
            CommandResponse protobuf message
            
        Raises:
            RuntimeError: If command fails or communication error occurs
        """
        try:
            response = self.zmq_client.send_command(command)
            if not response.success:
                logger.error(f"Command failed: {response.message}")
                raise RuntimeError(f"Command failed: {response.message}")
            return response
        except Exception as e:
            logger.error(f"Error sending command: {e}", exc_info=True)
            raise RuntimeError(f"Communication error: {e}")
    
    def create_ball(self, display_name: str) -> str:
        """
        Create a new ball in the registry.
        
        Args:
            display_name: User-friendly name for the ball (e.g., "Pink Ball #1")
            
        Returns:
            Unique ball ID (e.g., "ball_001")
            
        Raises:
            RuntimeError: If ball creation fails
        """
        logger.info(f"Creating ball with display name: {display_name}")
        
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.ENABLE_FEATURE
        command.feature_name = "create_ball"
        command.module_args["display_name"] = display_name
        
        response = self._send_command(command)
        
        # Parse ball ID from response message
        # Expected format: "Ball created with ID: ball_001"
        if "ID:" in response.message:
            ball_id = response.message.split("ID:")[-1].strip()
            logger.info(f"Ball created successfully: {ball_id}")
            return ball_id
        else:
            logger.warning(f"Unexpected response format: {response.message}")
            return response.message
    
    def delete_ball(self, ball_id: str) -> bool:
        """
        Delete a ball from the registry.
        
        Args:
            ball_id: Unique ID of the ball to delete
            
        Returns:
            True if ball was deleted successfully
            
        Raises:
            RuntimeError: If deletion fails
        """
        logger.info(f"Deleting ball: {ball_id}")
        
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.DISABLE_FEATURE
        command.feature_name = "delete_ball"
        command.module_args["ball_id"] = ball_id
        
        response = self._send_command(command)
        logger.info(f"Ball deleted successfully: {ball_id}")
        return True
    
    def get_all_balls(self) -> List[Dict]:
        """
        Get all registered balls (active and inactive).
        
        Returns:
            List of ball dictionaries with metadata
            
        Raises:
            RuntimeError: If query fails
        """
        logger.debug("Querying all balls")
        
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.ENABLE_FEATURE
        command.feature_name = "get_all_balls"
        
        response = self._send_command(command)
        
        # Parse JSON response
        try:
            balls = json.loads(response.message)
            logger.debug(f"Retrieved {len(balls)} balls")
            return balls
        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse balls JSON: {e}")
            return []
    
    def get_active_balls(self) -> List[Dict]:
        """
        Get only currently active balls.
        
        Returns:
            List of active ball dictionaries
            
        Raises:
            RuntimeError: If query fails
        """
        logger.debug("Querying active balls")
        
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.ENABLE_FEATURE
        command.feature_name = "get_active_balls"
        
        response = self._send_command(command)
        
        # Parse JSON response
        try:
            balls = json.loads(response.message)
            logger.debug(f"Retrieved {len(balls)} active balls")
            return balls
        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse active balls JSON: {e}")
            return []
    
    def activate_ball(self, ball_id: str) -> bool:
        """
        Activate a ball for tracking.
        
        Args:
            ball_id: Unique ID of the ball to activate
            
        Returns:
            True if ball was activated successfully
            
        Raises:
            RuntimeError: If activation fails (e.g., max active balls reached)
        """
        logger.info(f"Activating ball: {ball_id}")
        
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.ENABLE_FEATURE
        command.feature_name = "activate_ball"
        command.module_args["ball_id"] = ball_id
        
        response = self._send_command(command)
        logger.info(f"Ball activated successfully: {ball_id}")
        return True
    
    def deactivate_ball(self, ball_id: str) -> bool:
        """
        Deactivate a ball (stop tracking it).
        
        Args:
            ball_id: Unique ID of the ball to deactivate
            
        Returns:
            True if ball was deactivated successfully
            
        Raises:
            RuntimeError: If deactivation fails
        """
        logger.info(f"Deactivating ball: {ball_id}")
        
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.DISABLE_FEATURE
        command.feature_name = "deactivate_ball"
        command.module_args["ball_id"] = ball_id
        
        response = self._send_command(command)
        logger.info(f"Ball deactivated successfully: {ball_id}")
        return True
    
    def add_color_sample(self, ball_id: str, click_x: int, click_y: int, 
                        lighting: str = "unknown") -> bool:
        """
        Add a color calibration sample to a ball's profile.
        
        Args:
            ball_id: Unique ID of the ball
            click_x: X coordinate where user clicked to sample
            click_y: Y coordinate where user clicked to sample
            lighting: Description of lighting condition (e.g., "bright", "dim", "mixed")
            
        Returns:
            True if sample was added successfully
            
        Raises:
            RuntimeError: If adding sample fails
        """
        logger.info(f"Adding color sample to ball {ball_id} at ({click_x}, {click_y}), lighting: {lighting}")
        
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.CALIBRATE_COLOR
        command.color_name = ball_id
        command.click_x = click_x
        command.click_y = click_y
        command.module_args["lighting"] = lighting
        
        response = self._send_command(command)
        logger.info(f"Color sample added successfully to ball: {ball_id}")
        return True
    
    def remove_color_sample(self, ball_id: str, sample_index: int) -> bool:
        """
        Remove a color calibration sample from a ball's profile.
        
        Args:
            ball_id: Unique ID of the ball
            sample_index: Index of the sample to remove (0-based)
            
        Returns:
            True if sample was removed successfully
            
        Raises:
            RuntimeError: If removing sample fails
        """
        logger.info(f"Removing color sample {sample_index} from ball {ball_id}")
        
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.DISABLE_FEATURE
        command.feature_name = "remove_color_sample"
        command.module_args["ball_id"] = ball_id
        command.module_args["sample_index"] = str(sample_index)
        
        response = self._send_command(command)
        logger.info(f"Color sample removed successfully from ball: {ball_id}")
        return True
    
    def set_use_new_system(self, enabled: bool) -> bool:
        """
        Enable or disable the new ball tracking system.
        
        Args:
            enabled: True to enable new system, False for legacy system
            
        Returns:
            True if mode was changed successfully
            
        Raises:
            RuntimeError: If mode change fails
        """
        mode = "new" if enabled else "legacy"
        logger.info(f"Setting tracking mode to: {mode}")
        
        command = juggler_pb2.CommandRequest()
        if enabled:
            command.type = juggler_pb2.CommandRequest.ENABLE_FEATURE
            command.feature_name = "new_tracking_system"
        else:
            command.type = juggler_pb2.CommandRequest.DISABLE_FEATURE
            command.feature_name = "new_tracking_system"
        
        response = self._send_command(command)
        logger.info(f"Tracking mode set to: {mode}")
        return True
    
    def is_using_new_system(self) -> bool:
        """
        Check if the new tracking system is currently enabled.
        
        Returns:
            True if new system is enabled, False if using legacy system
            
        Raises:
            RuntimeError: If query fails
        """
        logger.debug("Querying tracking system mode")
        
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.ENABLE_FEATURE
        command.feature_name = "get_tracking_mode"
        
        response = self._send_command(command)
        
        # Parse response - expected: "new" or "legacy"
        is_new = "new" in response.message.lower()
        logger.debug(f"Tracking mode: {'new' if is_new else 'legacy'}")
        return is_new
    
    def save_registry(self, filepath: str = "ball_registry.json") -> bool:
        """
        Save the ball registry to a file.
        
        Args:
            filepath: Path to save the registry (default: "ball_registry.json")
            
        Returns:
            True if save was successful
            
        Raises:
            RuntimeError: If save fails
        """
        logger.info(f"Saving ball registry to: {filepath}")
        
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.ENABLE_FEATURE
        command.feature_name = "save_ball_registry"
        command.module_args["filepath"] = filepath
        
        response = self._send_command(command)
        logger.info(f"Ball registry saved successfully to: {filepath}")
        return True
    
    def load_registry(self, filepath: str = "ball_registry.json") -> bool:
        """
        Load the ball registry from a file.
        
        Args:
            filepath: Path to load the registry from (default: "ball_registry.json")
            
        Returns:
            True if load was successful
            
        Raises:
            RuntimeError: If load fails
        """
        logger.info(f"Loading ball registry from: {filepath}")
        
        command = juggler_pb2.CommandRequest()
        command.type = juggler_pb2.CommandRequest.ENABLE_FEATURE
        command.feature_name = "load_ball_registry"
        command.module_args["filepath"] = filepath
        
        response = self._send_command(command)
        logger.info(f"Ball registry loaded successfully from: {filepath}")
        return True