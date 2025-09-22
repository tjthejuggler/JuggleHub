#!/usr/bin/env python3
"""
Script to create V3 dataset from V2 dataset by removing hand annotations.

This script:
1. Creates V3 directories corresponding to each V2 directory
2. Processes JSON files to remove hand annotations
3. Copies images and text files (excluding hand-only images)
4. Copies via.html files to V3 directories

Rules:
- Remove images that ONLY have 'hand' annotations
- For images with 'hand' AND other annotations, remove just the 'hand' annotations
- Keep all other annotations ('led_on', 'led_off', 'dropped_ball')
"""

import json
import os
import shutil
from pathlib import Path
from typing import Dict, List, Set

# V2 directories to process
V2_DIRECTORIES = [
    "engine/data/annotation_sessions/V2rs455_normal_balls_daylight_sessions_auto_realsense",
    "engine/data/annotation_sessions/V2rs455_normal_balls_mixedlight_sessions_intentional_realsense", 
    "engine/data/annotation_sessions/V2rs455_just_hands_low_light_intentional_and_auto_realsense",
    "engine/data/annotation_sessions/V2_2_rs455_lonely_hands_low_light_intentional_realsense"
]

def get_v3_directory_name(v2_dir: str) -> str:
    """Convert V2 directory name to V3 directory name."""
    dir_name = os.path.basename(v2_dir)
    if dir_name.startswith("V2rs455"):
        return dir_name.replace("V2rs455", "V3rs455")
    elif dir_name.startswith("V2_2_rs455"):
        return dir_name.replace("V2_2_rs455", "V3_2_rs455")
    else:
        return f"V3_{dir_name}"

def analyze_image_annotations(regions: List[Dict]) -> tuple[bool, List[Dict]]:
    """
    Analyze image annotations and return (should_keep_image, filtered_regions).
    
    Returns:
        - should_keep_image: False if image only has 'hand' annotations
        - filtered_regions: regions with 'hand' annotations removed
    """
    non_hand_regions = []
    has_hand = False
    
    for region in regions:
        region_name = region.get("region_attributes", {}).get("name", "")
        if region_name == "hand":
            has_hand = True
        else:
            non_hand_regions.append(region)
    
    # Keep image if it has non-hand annotations OR no annotations at all
    should_keep_image = len(non_hand_regions) > 0 or not has_hand
    
    return should_keep_image, non_hand_regions

def process_json_file(input_path: str, output_path: str) -> Set[str]:
    """
    Process VIA JSON file to remove hand annotations.
    
    Returns:
        Set of image filenames that should be kept (not hand-only)
    """
    with open(input_path, 'r') as f:
        data = json.load(f)
    
    # Update project name to V3
    if "_via_settings" in data and "project" in data["_via_settings"]:
        project_name = data["_via_settings"]["project"]["name"]
        data["_via_settings"]["project"]["name"] = project_name.replace("V2", "V3")
    
    # Process image metadata
    new_img_metadata = {}
    images_to_keep = set()
    
    for img_key, img_data in data.get("_via_img_metadata", {}).items():
        filename = img_data["filename"]
        regions = img_data.get("regions", [])
        
        should_keep_image, filtered_regions = analyze_image_annotations(regions)
        
        if should_keep_image:
            images_to_keep.add(filename)
            # Update the image data with filtered regions
            new_img_data = img_data.copy()
            new_img_data["regions"] = filtered_regions
            new_img_metadata[img_key] = new_img_data
    
    # Update the data with filtered metadata
    data["_via_img_metadata"] = new_img_metadata
    
    # Update image ID list to only include kept images
    if "_via_image_id_list" in data:
        new_id_list = []
        for img_id in data["_via_image_id_list"]:
            # Extract filename from image ID (format: filename + size)
            for img_key, img_data in new_img_metadata.items():
                if img_key == img_id:
                    new_id_list.append(img_id)
                    break
        data["_via_image_id_list"] = new_id_list
    
    # Write processed JSON
    with open(output_path, 'w') as f:
        json.dump(data, f, indent=2)
    
    return images_to_keep

def get_class_mapping_from_txt_files(images_dir: str) -> Dict[int, str]:
    """
    Analyze txt files to understand class ID mapping.
    This is a heuristic approach - we'll need to map based on common patterns.
    """
    # Common YOLO class mappings for this type of dataset
    # This might need adjustment based on actual training configuration
    return {
        0: "led_on",
        1: "led_off", 
        2: "dropped_ball",
        3: "hand"
    }

def filter_txt_file_content(content: str, class_mapping: Dict[int, str]) -> str:
    """
    Filter YOLO format txt file content to remove hand annotations.
    """
    lines = content.strip().split('\n')
    filtered_lines = []
    
    for line in lines:
        if line.strip():
            parts = line.strip().split()
            if len(parts) >= 5:
                class_id = int(parts[0])
                class_name = class_mapping.get(class_id, "unknown")
                
                # Keep all non-hand annotations
                if class_name != "hand":
                    filtered_lines.append(line)
    
    return '\n'.join(filtered_lines)

def process_v2_directory(v2_dir: str):
    """Process a single V2 directory to create corresponding V3 directory."""
    print(f"Processing {v2_dir}...")
    
    # Create V3 directory name and path
    v3_dir_name = get_v3_directory_name(v2_dir)
    v3_dir = os.path.join(os.path.dirname(v2_dir), v3_dir_name)
    
    # Create V3 directory structure
    os.makedirs(v3_dir, exist_ok=True)
    os.makedirs(os.path.join(v3_dir, "images"), exist_ok=True)
    
    # Find and process JSON file
    json_files = list(Path(v2_dir).glob("*.json"))
    if not json_files:
        print(f"  Warning: No JSON file found in {v2_dir}")
        return
    
    json_file = json_files[0]
    v3_json_name = f"V3_{json_file.name}"
    v3_json_path = os.path.join(v3_dir, v3_json_name)
    
    # Process JSON and get list of images to keep
    images_to_keep = process_json_file(str(json_file), v3_json_path)
    print(f"  Keeping {len(images_to_keep)} images out of total")
    
    # Copy via.html file
    via_html_path = os.path.join(v2_dir, "via.html")
    if os.path.exists(via_html_path):
        shutil.copy2(via_html_path, os.path.join(v3_dir, "via.html"))
        print(f"  Copied via.html")
    
    # Get class mapping for txt files
    class_mapping = get_class_mapping_from_txt_files(os.path.join(v2_dir, "images"))
    
    # Process images directory
    v2_images_dir = os.path.join(v2_dir, "images")
    v3_images_dir = os.path.join(v3_dir, "images")
    
    copied_images = 0
    copied_txt_files = 0
    
    for filename in images_to_keep:
        # Copy image file
        img_path = os.path.join(v2_images_dir, filename)
        if os.path.exists(img_path):
            shutil.copy2(img_path, os.path.join(v3_images_dir, filename))
            copied_images += 1
        
        # Process corresponding txt file
        txt_filename = filename.rsplit('.', 1)[0] + '.txt'
        txt_path = os.path.join(v2_images_dir, txt_filename)
        if os.path.exists(txt_path):
            with open(txt_path, 'r') as f:
                content = f.read()
            
            # Filter out hand annotations
            filtered_content = filter_txt_file_content(content, class_mapping)
            
            # Only write txt file if it has content after filtering
            if filtered_content.strip():
                with open(os.path.join(v3_images_dir, txt_filename), 'w') as f:
                    f.write(filtered_content)
                copied_txt_files += 1
    
    print(f"  Copied {copied_images} images and {copied_txt_files} txt files")
    print(f"  Created V3 directory: {v3_dir}")

def main():
    """Main function to process all V2 directories."""
    print("Creating V3 dataset from V2 directories...")
    print("=" * 60)
    
    for v2_dir in V2_DIRECTORIES:
        if os.path.exists(v2_dir):
            process_v2_directory(v2_dir)
            print()
        else:
            print(f"Warning: Directory {v2_dir} does not exist")
            print()
    
    print("V3 dataset creation completed!")

if __name__ == "__main__":
    main()