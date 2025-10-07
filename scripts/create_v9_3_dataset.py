#!/usr/bin/env python3
"""
Create V9.3 Dataset from V8 and V9.1 datasets

This script:
1. Copies specified dataset directories and renames them to V9.3 versions
2. Adjusts bounding boxes in the annotation files:
   - Class 0 (ball): Makes boxes 2 pixels smaller on all sides
   - Class 1 (ball_held): Makes boxes 1 pixel larger on all sides

The adjustments are done in normalized YOLO format (0-1 range).
"""

import os
import shutil
from pathlib import Path
from typing import List, Tuple
import argparse

# Source dataset directories to copy
SOURCE_DATASETS = [
    "V8rs455_just_hands_low_light_intentional_and_auto_realsense",
    "V8rs455_led_balls_mixedlight_sessions_intentional_realsense",
    "V8rs455_normal_balls_daylight_sessions_auto_realsense",
    "V8rs455_normal_balls_mixedlight_sessions_intentional_realsense",
    "V8_intentional_edgecases_mixed_balls_normal_light_mixed_rs_no_boxes",
    "V8_ball_col_aug",
    "V8_hard_negatives",
    "V9.1_augmented",
    "V9.1_augmented_all",
    "V9.1_targeted_lowish_light_60fps_default_realsense"
]

# Image dimensions (assumed standard for the dataset)
IMAGE_WIDTH = 640
IMAGE_HEIGHT = 480


def get_v9_3_name(original_name: str) -> str:
    """Convert original dataset name to V9.3 version."""
    if original_name.startswith("V8"):
        return original_name.replace("V8", "V9.3", 1)
    elif original_name.startswith("V9.1"):
        return original_name.replace("V9.1", "V9.3", 1)
    else:
        return "V9.3_" + original_name


def adjust_bbox_size(center_x: float, center_y: float, width: float, height: float,
                     pixel_adjustment: int, img_width: int = IMAGE_WIDTH,
                     img_height: int = IMAGE_HEIGHT) -> Tuple[float, float, float, float]:
    """
    Adjust bounding box size by a pixel amount on all sides.
    
    Args:
        center_x, center_y: Normalized center coordinates (0-1)
        width, height: Normalized dimensions (0-1)
        pixel_adjustment: Pixels to add/subtract on each side (positive = larger, negative = smaller)
        img_width, img_height: Image dimensions in pixels
    
    Returns:
        Tuple of adjusted (center_x, center_y, width, height) in normalized format
    """
    # Convert normalized dimensions to pixels
    width_px = width * img_width
    height_px = height * img_height
    
    # Adjust by pixel amount on all sides (multiply by 2 since we adjust both sides)
    width_px += pixel_adjustment * 2
    height_px += pixel_adjustment * 2
    
    # Ensure dimensions don't go negative or exceed image bounds
    width_px = max(1.0, min(width_px, img_width))
    height_px = max(1.0, min(height_px, img_height))
    
    # Convert back to normalized
    new_width = width_px / img_width
    new_height = height_px / img_height
    
    # Ensure center stays within bounds after size adjustment
    half_width = new_width / 2
    half_height = new_height / 2
    
    new_center_x = max(half_width, min(center_x, 1.0 - half_width))
    new_center_y = max(half_height, min(center_y, 1.0 - half_height))
    
    return new_center_x, new_center_y, new_width, new_height


def process_annotation_file(txt_path: Path, output_path: Path) -> None:
    """
    Process a YOLO annotation file and adjust bounding boxes.
    
    Args:
        txt_path: Path to source annotation file
        output_path: Path to output annotation file
    """
    if not txt_path.exists():
        return
    
    with open(txt_path, 'r') as f:
        lines = f.readlines()
    
    adjusted_lines = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        
        parts = line.split()
        if len(parts) < 5:
            # Invalid line, keep as is
            adjusted_lines.append(line)
            continue
        
        try:
            class_id = int(parts[0])
            center_x = float(parts[1])
            center_y = float(parts[2])
            width = float(parts[3])
            height = float(parts[4])
            
            # Adjust based on class
            if class_id == 0:  # ball - make 2 pixels smaller on all sides
                new_cx, new_cy, new_w, new_h = adjust_bbox_size(
                    center_x, center_y, width, height, -2
                )
            elif class_id == 1:  # ball_held - make 1 pixel larger on all sides
                new_cx, new_cy, new_w, new_h = adjust_bbox_size(
                    center_x, center_y, width, height, 1
                )
            else:
                # Unknown class, keep as is
                new_cx, new_cy, new_w, new_h = center_x, center_y, width, height
            
            # Format with 6 decimal places for precision
            adjusted_line = f"{class_id} {new_cx:.6f} {new_cy:.6f} {new_w:.6f} {new_h:.6f}"
            adjusted_lines.append(adjusted_line)
            
        except (ValueError, IndexError) as e:
            # If parsing fails, keep original line
            print(f"Warning: Could not parse line in {txt_path}: {line}")
            adjusted_lines.append(line)
    
    # Write adjusted annotations
    with open(output_path, 'w') as f:
        f.write('\n'.join(adjusted_lines))
        if adjusted_lines:  # Add final newline if file has content
            f.write('\n')


def copy_and_adjust_dataset(source_dir: Path, dest_dir: Path, dry_run: bool = False) -> None:
    """
    Copy a dataset directory and adjust all annotation files.
    
    Args:
        source_dir: Source dataset directory
        dest_dir: Destination dataset directory
        dry_run: If True, only print what would be done
    """
    if not source_dir.exists():
        print(f"⚠️  Source directory does not exist: {source_dir}")
        return
    
    if dest_dir.exists():
        print(f"⚠️  Destination already exists: {dest_dir}")
        response = input("    Overwrite? (y/N): ")
        if response.lower() != 'y':
            print("    Skipping...")
            return
        if not dry_run:
            shutil.rmtree(dest_dir)
    
    print(f"📁 Processing: {source_dir.name} -> {dest_dir.name}")
    
    if dry_run:
        print(f"   [DRY RUN] Would copy directory structure")
        # Count files for dry run
        txt_count = len(list(source_dir.rglob("*.txt")))
        print(f"   [DRY RUN] Would process {txt_count} annotation files")
        return
    
    # Copy entire directory structure
    shutil.copytree(source_dir, dest_dir)
    print(f"   ✓ Copied directory structure")
    
    # Process all .txt files in the images subdirectory
    images_dir = dest_dir / "images"
    if not images_dir.exists():
        print(f"   ⚠️  No 'images' subdirectory found")
        return
    
    txt_files = list(images_dir.glob("*.txt"))
    processed_count = 0
    
    for txt_file in txt_files:
        process_annotation_file(txt_file, txt_file)
        processed_count += 1
    
    print(f"   ✓ Adjusted {processed_count} annotation files")


def main():
    parser = argparse.ArgumentParser(
        description="Create V9.3 dataset with adjusted bounding boxes",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Dry run to see what would be done
  python scripts/create_v9_3_dataset.py --dry-run
  
  # Actually create the datasets
  python scripts/create_v9_3_dataset.py
  
  # Use custom root directory
  python scripts/create_v9_3_dataset.py --root /path/to/annotation_sessions
        """
    )
    
    parser.add_argument(
        '--root',
        type=str,
        default='/home/twain/Projects/JuggleHub/engine/data/annotation_sessions',
        help='Root directory containing dataset folders'
    )
    
    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='Show what would be done without actually doing it'
    )
    
    parser.add_argument(
        '--datasets',
        nargs='+',
        help='Specific datasets to process (default: all predefined datasets)'
    )
    
    args = parser.parse_args()
    
    root_dir = Path(args.root)
    if not root_dir.exists():
        print(f"❌ Root directory does not exist: {root_dir}")
        return 1
    
    datasets_to_process = args.datasets if args.datasets else SOURCE_DATASETS
    
    print("=" * 70)
    print("V9.3 Dataset Creation Script")
    print("=" * 70)
    print(f"Root directory: {root_dir}")
    print(f"Mode: {'DRY RUN' if args.dry_run else 'LIVE'}")
    print(f"Datasets to process: {len(datasets_to_process)}")
    print()
    print("Adjustments:")
    print("  • Class 0 (ball): -2 pixels on all sides")
    print("  • Class 1 (ball_held): +1 pixel on all sides")
    print("=" * 70)
    print()
    
    if args.dry_run:
        print("🔍 DRY RUN MODE - No changes will be made")
        print()
    
    success_count = 0
    for dataset_name in datasets_to_process:
        source_path = root_dir / dataset_name
        dest_name = get_v9_3_name(dataset_name)
        dest_path = root_dir / dest_name
        
        try:
            copy_and_adjust_dataset(source_path, dest_path, args.dry_run)
            success_count += 1
        except Exception as e:
            print(f"❌ Error processing {dataset_name}: {e}")
        
        print()
    
    print("=" * 70)
    print(f"✅ Successfully processed {success_count}/{len(datasets_to_process)} datasets")
    if args.dry_run:
        print("   (This was a dry run - no actual changes were made)")
    print("=" * 70)
    
    return 0


if __name__ == "__main__":
    exit(main())