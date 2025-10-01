#!/usr/bin/env python3
"""
Script to create txt files for augmented images by finding and copying content
from corresponding source txt files.

For each augmented image in V8_ball_col_aug/images, this script:
1. Strips the augmentation suffix (_col_aug[1-3]_batch1.jpg)
2. Searches for the corresponding .txt file in source directories
3. Copies the txt content to a new file matching the augmented image name
"""

import os
import sys
from pathlib import Path

# Define paths
TARGET_DIR = Path("/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V8_ball_col_aug/images")

SOURCE_DIRS = [
    Path("/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V8rs455_lonely_hands_low_light_intentional_realsense/images"),
    Path("/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V8rs455_just_hands_low_light_intentional_and_auto_realsense/images"),
    Path("/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V8rs455_led_balls_mixedlight_sessions_intentional_realsense/images"),
    Path("/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V8rs455_normal_balls_daylight_sessions_auto_realsense/images"),
    Path("/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V8rs455_normal_balls_mixedlight_sessions_intentional_realsense/images"),
    Path("/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V8_intentional_edgecases_mixed_balls_normal_light_mixed_rs_no_boxes/images"),
]

# Augmentation suffixes to remove
AUG_SUFFIXES = [
    "_col_aug1_batch1.jpg",
    "_col_aug2_batch1.jpg",
    "_col_aug3_batch1.jpg",
]


def get_base_name(image_filename):
    """
    Extract the base name from an augmented image filename.
    
    Args:
        image_filename: Name of the augmented image file
        
    Returns:
        Base name without augmentation suffix, or None if no suffix matches
    """
    for suffix in AUG_SUFFIXES:
        if image_filename.endswith(suffix):
            return image_filename[:-len(suffix)]
    return None


def find_source_txt(base_name):
    """
    Find the source txt file matching the base name across all source directories.
    
    Args:
        base_name: Base name to search for (without extension)
        
    Returns:
        Path to the txt file if found, None otherwise
        
    Raises:
        ValueError: If multiple matching txt files are found
    """
    txt_filename = f"{base_name}.txt"
    found_paths = []
    
    for source_dir in SOURCE_DIRS:
        txt_path = source_dir / txt_filename
        if txt_path.exists():
            found_paths.append(txt_path)
    
    if len(found_paths) == 0:
        return None
    elif len(found_paths) == 1:
        return found_paths[0]
    else:
        raise ValueError(f"Found {len(found_paths)} matching txt files for '{txt_filename}': {found_paths}")


def process_images():
    """
    Process all augmented images in the target directory.
    
    Returns:
        Tuple of (success_count, skip_count, error_count)
    """
    if not TARGET_DIR.exists():
        print(f"Error: Target directory does not exist: {TARGET_DIR}")
        return 0, 0, 1
    
    # Get all image files
    image_files = [f for f in TARGET_DIR.iterdir() if f.is_file() and f.suffix.lower() == '.jpg']
    
    if not image_files:
        print(f"No image files found in {TARGET_DIR}")
        return 0, 0, 0
    
    print(f"Found {len(image_files)} image files to process")
    print()
    
    success_count = 0
    skip_count = 0
    error_count = 0
    
    for image_file in sorted(image_files):
        image_name = image_file.name
        
        # Get base name by removing augmentation suffix
        base_name = get_base_name(image_name)
        
        if base_name is None:
            print(f"⚠️  Skipping '{image_name}': No recognized augmentation suffix")
            skip_count += 1
            continue
        
        # Check if txt file already exists
        target_txt_path = TARGET_DIR / f"{image_file.stem}.txt"
        if target_txt_path.exists():
            print(f"⏭️  Skipping '{image_name}': txt file already exists")
            skip_count += 1
            continue
        
        # Find source txt file
        try:
            source_txt_path = find_source_txt(base_name)
            
            if source_txt_path is None:
                print(f"❌ Error for '{image_name}': No matching txt file found for base name '{base_name}'")
                error_count += 1
                continue
            
            # Read source txt content
            with open(source_txt_path, 'r') as f:
                txt_content = f.read()
            
            # Write to target txt file
            with open(target_txt_path, 'w') as f:
                f.write(txt_content)
            
            print(f"✅ Created '{target_txt_path.name}' from '{source_txt_path.parent.name}/{source_txt_path.name}'")
            success_count += 1
            
        except ValueError as e:
            print(f"❌ Error for '{image_name}': {e}")
            error_count += 1
        except Exception as e:
            print(f"❌ Unexpected error for '{image_name}': {e}")
            error_count += 1
    
    return success_count, skip_count, error_count


def main():
    """Main entry point."""
    print("=" * 80)
    print("Augmented Image TXT File Generator")
    print("=" * 80)
    print()
    
    # Verify source directories exist
    missing_dirs = [d for d in SOURCE_DIRS if not d.exists()]
    if missing_dirs:
        print("Error: The following source directories do not exist:")
        for d in missing_dirs:
            print(f"  - {d}")
        return 1
    
    print(f"Target directory: {TARGET_DIR}")
    print(f"Source directories: {len(SOURCE_DIRS)}")
    for d in SOURCE_DIRS:
        print(f"  - {d.name}")
    print()
    
    # Process images
    success_count, skip_count, error_count = process_images()
    
    # Print summary
    print()
    print("=" * 80)
    print("Summary")
    print("=" * 80)
    print(f"✅ Successfully created: {success_count} txt files")
    print(f"⏭️  Skipped: {skip_count} files")
    print(f"❌ Errors: {error_count} files")
    print()
    
    if error_count > 0:
        print("⚠️  Script completed with errors")
        return 1
    else:
        print("✨ Script completed successfully")
        return 0


if __name__ == "__main__":
    sys.exit(main())