#!/usr/bin/env python3
"""
Script to crop YOLO dataset images and update corresponding annotation files.

This script:
1. Crops images from 640x480 to 640x360 by removing 60 pixels from top and bottom
2. Updates YOLO annotation files to reflect the new image dimensions
3. Creates new versioned directories for the cropped datasets
"""

import os
import sys
import argparse
from pathlib import Path
from PIL import Image
import shutil


def parse_yolo_annotation(line):
    """Parse a single line of YOLO annotation."""
    parts = line.strip().split()
    if len(parts) != 5:
        return None
    
    class_id = int(parts[0])
    center_x = float(parts[1])
    center_y = float(parts[2])
    width = float(parts[3])
    height = float(parts[4])
    
    return {
        'class_id': class_id,
        'center_x': center_x,
        'center_y': center_y,
        'width': width,
        'height': height
    }


def adjust_annotation_for_crop(annotation, original_height, crop_top, crop_bottom):
    """
    Adjust YOLO annotation coordinates for cropped image.
    
    Args:
        annotation: Dict with YOLO annotation data
        original_height: Original image height in pixels
        crop_top: Pixels cropped from top
        crop_bottom: Pixels cropped from bottom
    
    Returns:
        Adjusted annotation dict or None if object is outside cropped area
    """
    new_height = original_height - crop_top - crop_bottom
    
    # Convert normalized coordinates to pixel coordinates
    center_y_px = annotation['center_y'] * original_height
    height_px = annotation['height'] * original_height
    
    # Calculate top and bottom of bounding box
    top_px = center_y_px - (height_px / 2)
    bottom_px = center_y_px + (height_px / 2)
    
    # Check if object is completely outside the cropped area
    if bottom_px <= crop_top or top_px >= (original_height - crop_bottom):
        return None
    
    # Clip bounding box to cropped area
    top_px_clipped = max(top_px, crop_top)
    bottom_px_clipped = min(bottom_px, original_height - crop_bottom)
    
    # Adjust coordinates relative to new image
    top_px_new = top_px_clipped - crop_top
    bottom_px_new = bottom_px_clipped - crop_top
    
    # Calculate new center and height
    center_y_px_new = (top_px_new + bottom_px_new) / 2
    height_px_new = bottom_px_new - top_px_new
    
    # Normalize to new image dimensions
    center_y_normalized = center_y_px_new / new_height
    height_normalized = height_px_new / new_height
    
    # X coordinates remain unchanged (no horizontal cropping)
    return {
        'class_id': annotation['class_id'],
        'center_x': annotation['center_x'],
        'center_y': center_y_normalized,
        'width': annotation['width'],
        'height': height_normalized
    }


def format_yolo_annotation(annotation):
    """Format annotation dict back to YOLO string format."""
    return f"{annotation['class_id']} {annotation['center_x']:.6f} {annotation['center_y']:.6f} {annotation['width']:.6f} {annotation['height']:.6f}"


def crop_image(image_path, output_path, crop_top, crop_bottom):
    """Crop image by removing pixels from top and bottom."""
    img = Image.open(image_path)
    width, height = img.size
    
    # Crop: (left, top, right, bottom)
    cropped = img.crop((0, crop_top, width, height - crop_bottom))
    cropped.save(output_path)
    
    return cropped.size


def process_annotation_file(txt_path, output_txt_path, original_height, crop_top, crop_bottom):
    """Process a YOLO annotation file and adjust coordinates for cropped image.
    
    Creates an empty txt file if no annotations exist or all annotations are cropped out.
    """
    adjusted_annotations = []
    
    # Process existing annotations if the file exists
    if os.path.exists(txt_path):
        with open(txt_path, 'r') as f:
            for line in f:
                annotation = parse_yolo_annotation(line)
                if annotation is None:
                    continue
                
                adjusted = adjust_annotation_for_crop(annotation, original_height, crop_top, crop_bottom)
                if adjusted is not None:
                    adjusted_annotations.append(adjusted)
    
    # Always write the output file (even if empty)
    with open(output_txt_path, 'w') as f:
        for annotation in adjusted_annotations:
            f.write(format_yolo_annotation(annotation) + '\n')


def process_annotation_session(session_dir, output_dir, original_height, crop_top, crop_bottom):
    """Process all images and annotations in an annotation session directory."""
    images_dir = os.path.join(session_dir, 'images')
    output_images_dir = os.path.join(output_dir, 'images')
    
    if not os.path.exists(images_dir):
        print(f"Warning: No 'images' directory found in {session_dir}")
        return 0
    
    # Create output directory
    os.makedirs(output_images_dir, exist_ok=True)
    
    # Get all image files
    image_files = [f for f in os.listdir(images_dir) if f.endswith('.jpg') or f.endswith('.png')]
    
    processed_count = 0
    for image_file in image_files:
        image_path = os.path.join(images_dir, image_file)
        output_image_path = os.path.join(output_images_dir, image_file)
        
        # Crop image
        try:
            crop_image(image_path, output_image_path, crop_top, crop_bottom)
        except Exception as e:
            print(f"Error cropping {image_file}: {e}")
            continue
        
        # Process corresponding annotation file (always create txt file for each image)
        base_name = os.path.splitext(image_file)[0]
        txt_file = base_name + '.txt'
        txt_path = os.path.join(images_dir, txt_file)
        output_txt_path = os.path.join(output_images_dir, txt_file)
        
        try:
            process_annotation_file(txt_path, output_txt_path, original_height, crop_top, crop_bottom)
        except Exception as e:
            print(f"Error processing annotation {txt_file}: {e}")
            continue
        
        processed_count += 1
        if processed_count % 100 == 0:
            print(f"  Processed {processed_count}/{len(image_files)} images...")
    
    print(f"  Completed: {processed_count} images processed")
    return processed_count


def main():
    parser = argparse.ArgumentParser(
        description='Crop YOLO dataset images and update annotations',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Example usage:
  python crop_dataset_and_update_annotations.py \\
    --sessions V8_intentional_edgecases_mixed_balls_normal_light_mixed_rs_no_boxes \\
               V8rs455_normal_balls_daylight_sessions_auto_realsense \\
    --old-version V8 \\
    --new-version V10 \\
    --original-height 480 \\
    --crop-top 60 \\
    --crop-bottom 60
        """
    )
    
    parser.add_argument(
        '--sessions',
        nargs='+',
        required=True,
        help='List of annotation session directory names (relative to annotation_sessions directory)'
    )
    
    parser.add_argument(
        '--old-version',
        required=True,
        help='Current version indicator (e.g., V8)'
    )
    
    parser.add_argument(
        '--new-version',
        required=True,
        help='New version indicator (e.g., V10)'
    )
    
    parser.add_argument(
        '--original-height',
        type=int,
        default=480,
        help='Original image height in pixels (default: 480)'
    )
    
    parser.add_argument(
        '--crop-top',
        type=int,
        default=60,
        help='Pixels to crop from top (default: 60)'
    )
    
    parser.add_argument(
        '--crop-bottom',
        type=int,
        default=60,
        help='Pixels to crop from bottom (default: 60)'
    )
    
    parser.add_argument(
        '--base-dir',
        default='engine/data/annotation_sessions',
        help='Base directory containing annotation sessions (default: engine/data/annotation_sessions)'
    )
    
    args = parser.parse_args()
    
    # Validate base directory
    base_dir = Path(args.base_dir)
    if not base_dir.exists():
        print(f"Error: Base directory '{base_dir}' does not exist")
        sys.exit(1)
    
    print(f"Cropping dataset from {args.original_height}px to {args.original_height - args.crop_top - args.crop_bottom}px")
    print(f"Crop: {args.crop_top}px from top, {args.crop_bottom}px from bottom")
    print(f"Version: {args.old_version} → {args.new_version}")
    print(f"Processing {len(args.sessions)} session(s)...\n")
    
    total_processed = 0
    
    for session_name in args.sessions:
        # Replace old version with new version in directory name
        new_session_name = session_name.replace(args.old_version, args.new_version, 1)
        
        session_dir = base_dir / session_name
        output_dir = base_dir / new_session_name
        
        if not session_dir.exists():
            print(f"Warning: Session directory '{session_dir}' does not exist, skipping...")
            continue
        
        if output_dir.exists():
            print(f"Warning: Output directory '{output_dir}' already exists!")
            response = input(f"Overwrite? (y/n): ")
            if response.lower() != 'y':
                print(f"Skipping {session_name}")
                continue
            shutil.rmtree(output_dir)
        
        print(f"Processing: {session_name} → {new_session_name}")
        
        count = process_annotation_session(
            str(session_dir),
            str(output_dir),
            args.original_height,
            args.crop_top,
            args.crop_bottom
        )
        
        total_processed += count
        print()
    
    print(f"Done! Total images processed: {total_processed}")


if __name__ == '__main__':
    main()