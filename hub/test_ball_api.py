#!/usr/bin/env python3
"""
Test script for Ball Management API

This script tests the Ball Management API endpoints to ensure they work correctly.
It can be run standalone or as part of automated testing.

Created: 2025-10-03
"""

import requests
import json
import sys
import time

# API base URL
BASE_URL = "http://localhost:5000/api"


def print_test(test_name: str):
    """Print test header."""
    print(f"\n{'='*60}")
    print(f"TEST: {test_name}")
    print('='*60)


def print_response(response):
    """Pretty print API response."""
    print(f"Status Code: {response.status_code}")
    try:
        data = response.json()
        print(f"Response: {json.dumps(data, indent=2)}")
        return data
    except:
        print(f"Response: {response.text}")
        return None


def test_health_check():
    """Test the health check endpoint."""
    print_test("Health Check")
    response = requests.get(f"{BASE_URL}/health")
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    print("✅ Health check passed")
    return True


def test_create_ball():
    """Test creating a new ball."""
    print_test("Create Ball")
    payload = {"display_name": "Test Pink Ball"}
    response = requests.post(f"{BASE_URL}/balls/create", json=payload)
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    assert 'ball_id' in data['data']
    ball_id = data['data']['ball_id']
    print(f"✅ Ball created with ID: {ball_id}")
    return ball_id


def test_get_all_balls():
    """Test getting all balls."""
    print_test("Get All Balls")
    response = requests.get(f"{BASE_URL}/balls")
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    assert 'balls' in data['data']
    print(f"✅ Retrieved {len(data['data']['balls'])} balls")
    return data['data']['balls']


def test_get_active_balls():
    """Test getting active balls."""
    print_test("Get Active Balls")
    response = requests.get(f"{BASE_URL}/balls/active")
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    assert 'balls' in data['data']
    print(f"✅ Retrieved {len(data['data']['balls'])} active balls")
    return data['data']['balls']


def test_activate_ball(ball_id: str):
    """Test activating a ball."""
    print_test(f"Activate Ball: {ball_id}")
    response = requests.post(f"{BASE_URL}/balls/{ball_id}/activate")
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    print(f"✅ Ball {ball_id} activated")
    return True


def test_deactivate_ball(ball_id: str):
    """Test deactivating a ball."""
    print_test(f"Deactivate Ball: {ball_id}")
    response = requests.post(f"{BASE_URL}/balls/{ball_id}/deactivate")
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    print(f"✅ Ball {ball_id} deactivated")
    return True


def test_add_color_sample(ball_id: str):
    """Test adding a color sample."""
    print_test(f"Add Color Sample to Ball: {ball_id}")
    payload = {
        "click_x": 320,
        "click_y": 240,
        "lighting": "bright"
    }
    response = requests.post(f"{BASE_URL}/balls/{ball_id}/samples", json=payload)
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    print(f"✅ Color sample added to ball {ball_id}")
    return True


def test_remove_color_sample(ball_id: str, sample_index: int = 0):
    """Test removing a color sample."""
    print_test(f"Remove Color Sample from Ball: {ball_id}")
    response = requests.delete(f"{BASE_URL}/balls/{ball_id}/samples/{sample_index}")
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    print(f"✅ Color sample {sample_index} removed from ball {ball_id}")
    return True


def test_set_tracking_mode(use_new_system: bool):
    """Test setting tracking mode."""
    mode = "new" if use_new_system else "legacy"
    print_test(f"Set Tracking Mode: {mode}")
    payload = {"use_new_system": use_new_system}
    response = requests.post(f"{BASE_URL}/tracking/mode", json=payload)
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    print(f"✅ Tracking mode set to: {mode}")
    return True


def test_get_tracking_mode():
    """Test getting tracking mode."""
    print_test("Get Tracking Mode")
    response = requests.get(f"{BASE_URL}/tracking/mode")
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    assert 'mode' in data['data']
    print(f"✅ Current tracking mode: {data['data']['mode']}")
    return data['data']['mode']


def test_save_registry():
    """Test saving ball registry."""
    print_test("Save Ball Registry")
    payload = {"filepath": "test_ball_registry.json"}
    response = requests.post(f"{BASE_URL}/balls/registry/save", json=payload)
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    print("✅ Ball registry saved")
    return True


def test_load_registry():
    """Test loading ball registry."""
    print_test("Load Ball Registry")
    payload = {"filepath": "test_ball_registry.json"}
    response = requests.post(f"{BASE_URL}/balls/registry/load", json=payload)
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    print("✅ Ball registry loaded")
    return True


def test_delete_ball(ball_id: str):
    """Test deleting a ball."""
    print_test(f"Delete Ball: {ball_id}")
    response = requests.delete(f"{BASE_URL}/balls/{ball_id}")
    data = print_response(response)
    assert response.status_code == 200
    assert data['success'] == True
    print(f"✅ Ball {ball_id} deleted")
    return True


def run_all_tests():
    """Run all API tests."""
    print("\n" + "="*60)
    print("BALL MANAGEMENT API TEST SUITE")
    print("="*60)
    print(f"Testing API at: {BASE_URL}")
    
    try:
        # Basic tests
        test_health_check()
        
        # Ball management tests
        ball_id = test_create_ball()
        test_get_all_balls()
        test_get_active_balls()
        
        # Activation tests
        test_activate_ball(ball_id)
        test_get_active_balls()
        test_deactivate_ball(ball_id)
        test_get_active_balls()
        
        # Color calibration tests
        test_add_color_sample(ball_id)
        test_remove_color_sample(ball_id, 0)
        
        # Tracking mode tests
        test_set_tracking_mode(True)
        test_get_tracking_mode()
        test_set_tracking_mode(False)
        test_get_tracking_mode()
        
        # Persistence tests
        test_save_registry()
        test_load_registry()
        
        # Cleanup
        test_delete_ball(ball_id)
        
        print("\n" + "="*60)
        print("✅ ALL TESTS PASSED!")
        print("="*60)
        return True
        
    except AssertionError as e:
        print(f"\n❌ TEST FAILED: {e}")
        return False
    except requests.exceptions.ConnectionError:
        print(f"\n❌ CONNECTION ERROR: Could not connect to API at {BASE_URL}")
        print("Make sure the Hub is running with API enabled:")
        print("  python3 hub/main.py --no-ui")
        return False
    except Exception as e:
        print(f"\n❌ UNEXPECTED ERROR: {e}")
        import traceback
        traceback.print_exc()
        return False


if __name__ == '__main__':
    success = run_all_tests()
    sys.exit(0 if success else 1)