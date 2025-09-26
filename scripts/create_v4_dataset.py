#!/usr/bin/env python3
"""
Create V4 dataset from existing annotation sessions.
- Remove all 'hand' annotations
- Convert 'led_on', 'led_off', 'dropped_ball' to 'ball' (class 0)
- Copy images and via.html files
- Create new JSON files with V4 prefix
"""

import os
import json
import shutil
from pathlib import Path
import argparse
from collections import defaultdict

# Source directories to process
SOURCE_DIRS = [
    "V2rs455_normal_balls_daylight_sessions_auto_realsense",
    "V2rs455_normal_balls_mixedlight_sessions_intentional_realsense",
    "V2rs455_just_hands_low_light_intentional_and_auto_realsense",
    "V2_2_rs455_lonely_hands_low_light_intentional_realsense",
    "rs455_led_balls_mixedlight_sessions_intentional_realsense"
]

# Base path for annotation sessions
ANNOTATION_SESSIONS_BASE = "engine/data/annotation_sessions"

def get_v4_directory_name(source_dir):
    """Generate V4 directory name from source directory."""
    dir_name = os.path.basename(source_dir)
    return f"V4{dir_name}"

def analyze_json_annotations(json_path):
    """
    Analyze JSON file to understand class mappings and identify hand regions.
    Returns a mapping of image names to regions that should be kept.
    """
    with open(json_path, 'r') as f:
        data = json.load(f)
    
    image_annotations = {}
    
    if '_via_img_metadata' in data:
        for img_key, img_data in data['_via_img_metadata'].items():
            filename = img_data.get('filename', '')
            if not filename:
                continue
                
            # Extract base filename without extension for matching with txt files
            base_name = os.path.splitext(filename)[0]
            
            ball_regions = []
            if 'regions' in img_data:
                for i, region in enumerate(img_data['regions']):
                    if 'region_attributes' in region and 'name' in region['region_attributes']:
                        class_name = region['region_attributes']['name']
                        
                        # Keep only ball-related classes, skip hand
                        if class_name in ['led_on', 'led_off', 'dropped_ball']:
                            ball_regions.append(i)  # Store region index
            
            image_annotations[base_name] = ball_regions
    
    return image_annotations

def process_text_file(input_path, output_path, json_annotations):
    """
    Process YOLO format text file:
    - Remove lines corresponding to 'hand' annotations
    - Convert all remaining classes to class 0 (ball)
    """
    if not os.path.exists(input_path):
        return
    
    # Get base filename for lookup
    base_name = os.path.splitext(os.path.basename(input_path))[0]
    
    # Get the regions that should be kept (non-hand regions)
    regions_to_keep = json_annotations.get(base_name, [])
    
    processed_lines = []
    
    with open(input_path, 'r') as f:
        lines = f.readlines()
        
        # If we have JSON annotation info, use it to filter
        if base_name in json_annotations:
            for i, region_idx in enumerate(regions_to_keep):
                if region_idx < len(lines):
                    line = lines[region_idx].strip()
                    if line:
                        parts = line.split()
                        if len(parts) >= 5:
                            # Convert to ball class (0)
                            parts[0] = '0'
                            processed_lines.append(' '.join(parts))
        else:
            # Fallback: convert all classes to ball (assume no hands if no JSON info)
            for line in lines:
                line = line.strip()
                if line:
                    parts = line.split()
                    if len(parts) >= 5:
                        # Convert to ball class (0)
                        parts[0] = '0'
                        processed_lines.append(' '.join(parts))
    
    # Write processed lines
    with open(output_path, 'w') as f:
        for line in processed_lines:
            f.write(line + '\n')

def process_json_file(input_path, output_path):
    """
    Process VIA JSON file:
    - Remove all 'hand' regions
    - Convert 'led_on', 'led_off', 'dropped_ball' to 'ball'
    - Update project name to include V4
    """
    with open(input_path, 'r') as f:
        data = json.load(f)
    
    # Update project name
    if '_via_settings' in data and 'project' in data['_via_settings']:
        old_name = data['_via_settings']['project']['name']
        data['_via_settings']['project']['name'] = f"V4{old_name}"
    
    # Update region attributes to only include 'ball'
    if '_via_attributes' in data and 'region' in data['_via_attributes']:
        if 'name' in data['_via_attributes']['region']:
            data['_via_attributes']['region']['name']['options'] = {"ball": ""}
            data['_via_attributes']['region']['name']['default_options'] = {"ball": True}
    
    # Process image metadata
    if '_via_img_metadata' in data:
        for img_key, img_data in data['_via_img_metadata'].items():
            if 'regions' in img_data:
                new_regions = []
                
                for region in img_data['regions']:
                    if 'region_attributes' in region and 'name' in region['region_attributes']:
                        class_name = region['region_attributes']['name']
                        
                        # Skip hand regions
                        if class_name == 'hand':
                            continue
                        
                        # Convert ball-related classes to 'ball'
                        if class_name in ['led_on', 'led_off', 'dropped_ball']:
                            region['region_attributes']['name'] = 'ball'
                            new_regions.append(region)
                
                img_data['regions'] = new_regions
    
    # Write processed JSON
    with open(output_path, 'w') as f:
        json.dump(data, f, indent=2)

def create_v4_dataset(source_base_dir, output_base_dir, dry_run=False):
    """Create V4 dataset from source directories."""
    
    for source_dir in SOURCE_DIRS:
        source_path = os.path.join(source_base_dir, ANNOTATION_SESSIONS_BASE, source_dir)
        
        if not os.path.exists(source_path):
            print(f"Warning: Source directory not found: {source_path}")
            continue
        
        # Create V4 directory name
        v4_dir_name = get_v4_directory_name(source_dir)
        output_dir = os.path.join(output_base_dir, ANNOTATION_SESSIONS_BASE, v4_dir_name)
        
        print(f"Processing: {source_dir} -> {v4_dir_name}")
        
        if not dry_run:
            # Create output directory structure
            os.makedirs(output_dir, exist_ok=True)
            os.makedirs(os.path.join(output_dir, "images"), exist_ok=True)
        
        # First, analyze JSON file to understand annotations
        json_annotations = {}
        json_source_path = None
        for filename in os.listdir(source_path):
            if filename.endswith('.json'):
                json_source_path = os.path.join(source_path, filename)
                print(f"  Analyzing JSON: {filename}")
                if not dry_run:
                    json_annotations = analyze_json_annotations(json_source_path)
                break
        
        # Process files
        images_source = os.path.join(source_path, "images")
        images_output = os.path.join(output_dir, "images")
        
        if os.path.exists(images_source):
            for filename in os.listdir(images_source):
                source_file = os.path.join(images_source, filename)
                output_file = os.path.join(images_output, filename)
                
                if filename.endswith('.jpg'):
                    # Copy image files
                    print(f"  Copying image: {filename}")
                    if not dry_run:
                        shutil.copy2(source_file, output_file)
                
                elif filename.endswith('.txt'):
                    # Process text annotation files
                    print(f"  Processing text file: {filename}")
                    if not dry_run:
                        process_text_file(source_file, output_file, json_annotations)
        
        # Copy and process via.html
        via_source = os.path.join(source_path, "via.html")
        via_output = os.path.join(output_dir, "via.html")
        if os.path.exists(via_source):
            print(f"  Copying via.html")
            if not dry_run:
                shutil.copy2(via_source, via_output)
        
        # Process JSON file
        if json_source_path:
            filename = os.path.basename(json_source_path)
            # Add V4 prefix to JSON filename
            json_output = os.path.join(output_dir, f"V4{filename}")
            print(f"  Processing JSON: {filename} -> V4{filename}")
            if not dry_run:
                process_json_file(json_source_path, json_output)
        
        print(f"  Completed: {v4_dir_name}")
        print()

def main():
    parser = argparse.ArgumentParser(description='Create V4 dataset')
    parser.add_argument('--source', default='.', help='Source base directory (default: current directory)')
    parser.add_argument('--output', default='.', help='Output base directory (default: current directory)')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be done without actually doing it')
    
    args = parser.parse_args()
    
    print("Creating V4 dataset...")
    print(f"Source base: {args.source}")
    print(f"Output base: {args.output}")
    print(f"Dry run: {args.dry_run}")
    print()
    
    create_v4_dataset(args.source, args.output, args.dry_run)
    
    if args.dry_run:
        print("Dry run completed. Use without --dry-run to actually create the dataset.")
    else:
        print("V4 dataset creation completed!")

if __name__ == "__main__":
    main()