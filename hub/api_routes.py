#!/usr/bin/env python3
"""
API Routes - Flask REST API for ball management

This module provides HTTP REST API endpoints for managing balls through the
BallManager interface. It handles ball creation, deletion, activation, color
calibration, and tracking mode management.

Created: 2025-10-03
"""

import logging
from flask import Flask, request, jsonify, Blueprint
from typing import Dict, Any
from ball_manager import BallManager

logger = logging.getLogger(__name__)

# Create a Blueprint for ball management routes
ball_api = Blueprint('ball_api', __name__, url_prefix='/api')

# Global BallManager instance (will be initialized by main app)
_ball_manager: BallManager = None


def init_ball_api(ball_manager: BallManager):
    """
    Initialize the ball API with a BallManager instance.
    
    Args:
        ball_manager: BallManager instance to use for all operations
    """
    global _ball_manager
    _ball_manager = ball_manager
    logger.info("Ball API initialized")


def _get_ball_manager() -> BallManager:
    """Get the global BallManager instance."""
    if _ball_manager is None:
        raise RuntimeError("BallManager not initialized. Call init_ball_api() first.")
    return _ball_manager


def _success_response(data: Any = None, message: str = "Success") -> Dict:
    """
    Create a successful JSON response.
    
    Args:
        data: Optional data to include in response
        message: Success message
        
    Returns:
        Dictionary for JSON response
    """
    response = {
        "success": True,
        "message": message
    }
    if data is not None:
        response["data"] = data
    return response


def _error_response(message: str, status_code: int = 400) -> tuple:
    """
    Create an error JSON response.
    
    Args:
        message: Error message
        status_code: HTTP status code
        
    Returns:
        Tuple of (response_dict, status_code)
    """
    return {
        "success": False,
        "error": message
    }, status_code


# ===== Ball Management Endpoints =====

@ball_api.route('/balls/create', methods=['POST'])
def create_ball():
    """
    Create a new ball.
    
    Request JSON:
        {
            "display_name": "Pink Ball #1"
        }
    
    Response JSON:
        {
            "success": true,
            "message": "Ball created successfully",
            "data": {
                "ball_id": "ball_001"
            }
        }
    """
    try:
        data = request.get_json()
        if not data or 'display_name' not in data:
            return _error_response("Missing required field: display_name")
        
        display_name = data['display_name']
        ball_manager = _get_ball_manager()
        
        ball_id = ball_manager.create_ball(display_name)
        
        return jsonify(_success_response(
            data={"ball_id": ball_id},
            message="Ball created successfully"
        ))
        
    except Exception as e:
        logger.error(f"Error creating ball: {e}", exc_info=True)
        return _error_response(str(e), 500)


@ball_api.route('/balls/<ball_id>', methods=['DELETE'])
def delete_ball(ball_id: str):
    """
    Delete a ball.
    
    URL Parameters:
        ball_id: Unique ID of the ball to delete
    
    Response JSON:
        {
            "success": true,
            "message": "Ball deleted successfully"
        }
    """
    try:
        ball_manager = _get_ball_manager()
        ball_manager.delete_ball(ball_id)
        
        return jsonify(_success_response(
            message=f"Ball {ball_id} deleted successfully"
        ))
        
    except Exception as e:
        logger.error(f"Error deleting ball {ball_id}: {e}", exc_info=True)
        return _error_response(str(e), 500)


@ball_api.route('/balls', methods=['GET'])
def get_all_balls():
    """
    Get all registered balls (active and inactive).
    
    Response JSON:
        {
            "success": true,
            "message": "Success",
            "data": {
                "balls": [
                    {
                        "id": "ball_001",
                        "display_name": "Pink Ball #1",
                        "is_active": true,
                        "logical_tracker_id": 0,
                        ...
                    }
                ]
            }
        }
    """
    try:
        ball_manager = _get_ball_manager()
        balls = ball_manager.get_all_balls()
        
        return jsonify(_success_response(
            data={"balls": balls},
            message=f"Retrieved {len(balls)} balls"
        ))
        
    except Exception as e:
        logger.error(f"Error getting all balls: {e}", exc_info=True)
        return _error_response(str(e), 500)


@ball_api.route('/balls/active', methods=['GET'])
def get_active_balls():
    """
    Get only currently active balls.
    
    Response JSON:
        {
            "success": true,
            "message": "Success",
            "data": {
                "balls": [...]
            }
        }
    """
    try:
        ball_manager = _get_ball_manager()
        balls = ball_manager.get_active_balls()
        
        return jsonify(_success_response(
            data={"balls": balls},
            message=f"Retrieved {len(balls)} active balls"
        ))
        
    except Exception as e:
        logger.error(f"Error getting active balls: {e}", exc_info=True)
        return _error_response(str(e), 500)


@ball_api.route('/balls/<ball_id>/activate', methods=['POST'])
def activate_ball(ball_id: str):
    """
    Activate a ball for tracking.
    
    URL Parameters:
        ball_id: Unique ID of the ball to activate
    
    Response JSON:
        {
            "success": true,
            "message": "Ball activated successfully"
        }
    """
    try:
        ball_manager = _get_ball_manager()
        ball_manager.activate_ball(ball_id)
        
        return jsonify(_success_response(
            message=f"Ball {ball_id} activated successfully"
        ))
        
    except Exception as e:
        logger.error(f"Error activating ball {ball_id}: {e}", exc_info=True)
        return _error_response(str(e), 500)


@ball_api.route('/balls/<ball_id>/deactivate', methods=['POST'])
def deactivate_ball(ball_id: str):
    """
    Deactivate a ball (stop tracking it).
    
    URL Parameters:
        ball_id: Unique ID of the ball to deactivate
    
    Response JSON:
        {
            "success": true,
            "message": "Ball deactivated successfully"
        }
    """
    try:
        ball_manager = _get_ball_manager()
        ball_manager.deactivate_ball(ball_id)
        
        return jsonify(_success_response(
            message=f"Ball {ball_id} deactivated successfully"
        ))
        
    except Exception as e:
        logger.error(f"Error deactivating ball {ball_id}: {e}", exc_info=True)
        return _error_response(str(e), 500)


# ===== Color Calibration Endpoints =====

@ball_api.route('/balls/<ball_id>/samples', methods=['POST'])
def add_color_sample(ball_id: str):
    """
    Add a color calibration sample to a ball's profile.
    
    URL Parameters:
        ball_id: Unique ID of the ball
    
    Request JSON:
        {
            "click_x": 320,
            "click_y": 240,
            "lighting": "bright"  // optional, default: "unknown"
        }
    
    Response JSON:
        {
            "success": true,
            "message": "Color sample added successfully"
        }
    """
    try:
        data = request.get_json()
        if not data or 'click_x' not in data or 'click_y' not in data:
            return _error_response("Missing required fields: click_x, click_y")
        
        click_x = int(data['click_x'])
        click_y = int(data['click_y'])
        lighting = data.get('lighting', 'unknown')
        
        ball_manager = _get_ball_manager()
        ball_manager.add_color_sample(ball_id, click_x, click_y, lighting)
        
        return jsonify(_success_response(
            message=f"Color sample added to ball {ball_id}"
        ))
        
    except ValueError as e:
        return _error_response(f"Invalid coordinate values: {e}")
    except Exception as e:
        logger.error(f"Error adding color sample to ball {ball_id}: {e}", exc_info=True)
        return _error_response(str(e), 500)


@ball_api.route('/balls/<ball_id>/samples/<int:sample_index>', methods=['DELETE'])
def remove_color_sample(ball_id: str, sample_index: int):
    """
    Remove a color calibration sample from a ball's profile.
    
    URL Parameters:
        ball_id: Unique ID of the ball
        sample_index: Index of the sample to remove (0-based)
    
    Response JSON:
        {
            "success": true,
            "message": "Color sample removed successfully"
        }
    """
    try:
        ball_manager = _get_ball_manager()
        ball_manager.remove_color_sample(ball_id, sample_index)
        
        return jsonify(_success_response(
            message=f"Color sample {sample_index} removed from ball {ball_id}"
        ))
        
    except Exception as e:
        logger.error(f"Error removing color sample from ball {ball_id}: {e}", exc_info=True)
        return _error_response(str(e), 500)


# ===== Tracking Mode Endpoints =====

@ball_api.route('/tracking/mode', methods=['POST'])
def set_tracking_mode():
    """
    Set the tracking mode (new system or legacy).
    
    Request JSON:
        {
            "use_new_system": true
        }
    
    Response JSON:
        {
            "success": true,
            "message": "Tracking mode set to: new"
        }
    """
    try:
        data = request.get_json()
        if not data or 'use_new_system' not in data:
            return _error_response("Missing required field: use_new_system")
        
        use_new_system = bool(data['use_new_system'])
        
        ball_manager = _get_ball_manager()
        ball_manager.set_use_new_system(use_new_system)
        
        mode = "new" if use_new_system else "legacy"
        return jsonify(_success_response(
            message=f"Tracking mode set to: {mode}"
        ))
        
    except Exception as e:
        logger.error(f"Error setting tracking mode: {e}", exc_info=True)
        return _error_response(str(e), 500)


@ball_api.route('/tracking/mode', methods=['GET'])
def get_tracking_mode():
    """
    Get the current tracking mode.
    
    Response JSON:
        {
            "success": true,
            "message": "Success",
            "data": {
                "use_new_system": true,
                "mode": "new"
            }
        }
    """
    try:
        ball_manager = _get_ball_manager()
        use_new_system = ball_manager.is_using_new_system()
        
        return jsonify(_success_response(
            data={
                "use_new_system": use_new_system,
                "mode": "new" if use_new_system else "legacy"
            }
        ))
        
    except Exception as e:
        logger.error(f"Error getting tracking mode: {e}", exc_info=True)
        return _error_response(str(e), 500)


# ===== Registry Persistence Endpoints =====

@ball_api.route('/balls/registry/save', methods=['POST'])
def save_registry():
    """
    Save the ball registry to a file.
    
    Request JSON (optional):
        {
            "filepath": "custom_registry.json"
        }
    
    Response JSON:
        {
            "success": true,
            "message": "Ball registry saved successfully"
        }
    """
    try:
        data = request.get_json() or {}
        filepath = data.get('filepath', 'ball_registry.json')
        
        ball_manager = _get_ball_manager()
        ball_manager.save_registry(filepath)
        
        return jsonify(_success_response(
            message=f"Ball registry saved to: {filepath}"
        ))
        
    except Exception as e:
        logger.error(f"Error saving ball registry: {e}", exc_info=True)
        return _error_response(str(e), 500)


@ball_api.route('/balls/registry/load', methods=['POST'])
def load_registry():
    """
    Load the ball registry from a file.
    
    Request JSON (optional):
        {
            "filepath": "custom_registry.json"
        }
    
    Response JSON:
        {
            "success": true,
            "message": "Ball registry loaded successfully"
        }
    """
    try:
        data = request.get_json() or {}
        filepath = data.get('filepath', 'ball_registry.json')
        
        ball_manager = _get_ball_manager()
        ball_manager.load_registry(filepath)
        
        return jsonify(_success_response(
            message=f"Ball registry loaded from: {filepath}"
        ))
        
    except Exception as e:
        logger.error(f"Error loading ball registry: {e}", exc_info=True)
        return _error_response(str(e), 500)


# ===== Health Check Endpoint =====

@ball_api.route('/health', methods=['GET'])
def health_check():
    """
    Health check endpoint.
    
    Response JSON:
        {
            "success": true,
            "message": "Ball API is healthy"
        }
    """
    return jsonify(_success_response(message="Ball API is healthy"))


def create_ball_api_app(ball_manager: BallManager = None) -> Flask:
    """
    Create a standalone Flask app with ball API routes.
    
    This is useful for testing or running the API as a separate service.
    
    Args:
        ball_manager: Optional BallManager instance. If None, creates a new one.
        
    Returns:
        Flask application instance
    """
    app = Flask(__name__)
    
    # Initialize ball manager
    if ball_manager is None:
        ball_manager = BallManager()
    init_ball_api(ball_manager)
    
    # Register blueprint
    app.register_blueprint(ball_api)
    
    # Add CORS headers for development
    @app.after_request
    def after_request(response):
        response.headers.add('Access-Control-Allow-Origin', '*')
        response.headers.add('Access-Control-Allow-Headers', 'Content-Type,Authorization')
        response.headers.add('Access-Control-Allow-Methods', 'GET,PUT,POST,DELETE,OPTIONS')
        return response
    
    logger.info("Ball API Flask app created")
    return app


if __name__ == '__main__':
    # Run as standalone API server for testing
    logging.basicConfig(
        level=logging.DEBUG,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    
    app = create_ball_api_app()
    print("🚀 Starting Ball API server on http://localhost:5000")
    print("📚 API Documentation:")
    print("  POST   /api/balls/create")
    print("  DELETE /api/balls/<ball_id>")
    print("  GET    /api/balls")
    print("  GET    /api/balls/active")
    print("  POST   /api/balls/<ball_id>/activate")
    print("  POST   /api/balls/<ball_id>/deactivate")
    print("  POST   /api/balls/<ball_id>/samples")
    print("  DELETE /api/balls/<ball_id>/samples/<index>")
    print("  POST   /api/tracking/mode")
    print("  GET    /api/tracking/mode")
    print("  POST   /api/balls/registry/save")
    print("  POST   /api/balls/registry/load")
    print("  GET    /api/health")
    
    app.run(host='0.0.0.0', port=5000, debug=True)