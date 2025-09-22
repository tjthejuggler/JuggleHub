#!/usr/bin/env python3
"""
Quick fix to update project names in V3 JSON files to include V3 prefix.
"""

import json
import os
from pathlib import Path

V3_DIRECTORIES = [
    "engine/data/annotation_sessions/V3rs455_normal_balls_daylight_sessions_auto_realsense",
    "engine/data/annotation_sessions/V3rs455_normal_balls_mixedlight_sessions_intentional_realsense", 
    "engine/data/annotation_sessions/V3rs455_just_hands_low_light_intentional_and_auto_realsense",
    "engine/data/annotation_sessions/V3_2_rs455_lonely_hands_low_light_intentional_realsense"
]

def fix_project_name(json_path: str):
    """Fix the project name in a V3 JSON file."""
    with open(json_path, 'r') as f:
        data = json.load(f)
    
    # Update project name to V3
    if "_via_settings" in data and "project" in data["_via_settings"]:
        project_name = data["_via_settings"]["project"]["name"]
        if not project_name.startswith("V3_"):
            data["_via_settings"]["project"]["name"] = f"V3_{project_name}"
            
            # Write back the updated JSON
            with open(json_path, 'w') as f:
                json.dump(data, f, indent=2)
            
            print(f"Updated project name in {json_path}")

def main():
    """Fix project names in all V3 JSON files."""
    for v3_dir in V3_DIRECTORIES:
        if os.path.exists(v3_dir):
            json_files = list(Path(v3_dir).glob("*.json"))
            for json_file in json_files:
                fix_project_name(str(json_file))

if __name__ == "__main__":
    main()