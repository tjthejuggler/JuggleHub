#!/usr/bin/env python3
"""
Script to visualize YOLO annotations on images.

This script displays an image with bounding boxes overlaid based on the
corresponding YOLO annotation file. Useful for verifying that annotations
are correct after dataset transformations.
"""

import os
import sys
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import matplotlib.pyplot as plt
import matplotlib.patches as patches


def parse_yolo_annotation(line):
    """Parse a single line of YOLO annotation."""
    parts = line.strip().split()
    if len(parts) != 5:
        return None
    
    try:
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
    except ValueError:
        return None


def read_annotations(txt_path):
    """Read all annotations from a YOLO annotation file."""
    annotations = []
    
    if not os.path.exists(txt_path):
        return annotations
    
    with open(txt_path, 'r') as f:
        for line in f:
            annotation = parse_yolo_annotation(line)
            if annotation is not None:
                annotations.append(annotation)
    
    return annotations


def yolo_to_bbox(annotation, img_width, img_height):
    """Convert YOLO format (normalized center x, y, w, h) to pixel coordinates (x1, y1, x2, y2)."""
    center_x_px = annotation['center_x'] * img_width
    center_y_px = annotation['center_y'] * img_height
    width_px = annotation['width'] * img_width
    height_px = annotation['height'] * img_height
    
    x1 = center_x_px - (width_px / 2)
    y1 = center_y_px - (height_px / 2)
    x2 = center_x_px + (width_px / 2)
    y2 = center_y_px + (height_px / 2)
    
    return x1, y1, x2, y2


def visualize_annotations(image_path, txt_path=None, class_names=None):
    """
    Visualize YOLO annotations on an image.
    
    Args:
        image_path: Path to the image file
        txt_path: Path to the annotation file (if None, will look for .txt with same name as image)
        class_names: Dict mapping class IDs to names (optional)
    """
    # Load image
    if not os.path.exists(image_path):
        print(f"Error: Image file '{image_path}' does not exist")
        return False
    
    img = Image.open(image_path)
    img_width, img_height = img.size
    
    # Determine annotation file path
    if txt_path is None:
        base_name = os.path.splitext(image_path)[0]
        txt_path = base_name + '.txt'
    
    # Read annotations
    annotations = read_annotations(txt_path)
    
    # Create figure
    fig, ax = plt.subplots(1, figsize=(12, 8))
    ax.imshow(img)
    
    # Draw bounding boxes
    colors = ['red', 'blue', 'green', 'yellow', 'cyan', 'magenta']
    
    for i, annotation in enumerate(annotations):
        x1, y1, x2, y2 = yolo_to_bbox(annotation, img_width, img_height)
        
        # Choose color based on class
        color = colors[annotation['class_id'] % len(colors)]
        
        # Create rectangle
        rect = patches.Rectangle(
            (x1, y1), x2 - x1, y2 - y1,
            linewidth=2,
            edgecolor=color,
            facecolor='none'
        )
        ax.add_patch(rect)
        
        # Add label
        if class_names and annotation['class_id'] in class_names:
            label = f"{class_names[annotation['class_id']]} ({annotation['class_id']})"
        else:
            label = f"Class {annotation['class_id']}"
        
        ax.text(
            x1, y1 - 5,
            label,
            color=color,
            fontsize=10,
            bbox=dict(facecolor='white', alpha=0.7, edgecolor=color, boxstyle='round,pad=0.3')
        )
    
    # Set title
    title = f"Image: {os.path.basename(image_path)}\n"
    title += f"Size: {img_width}x{img_height} | Annotations: {len(annotations)}"
    if not os.path.exists(txt_path):
        title += " (no annotation file found)"
    elif len(annotations) == 0:
        title += " (empty annotation file)"
    
    ax.set_title(title, fontsize=12, pad=10)
    ax.axis('off')
    
    plt.tight_layout()
    plt.show()
    
    return True


def main():
    parser = argparse.ArgumentParser(
        description='Visualize YOLO annotations on images',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Visualize a single image (will look for matching .txt file)
  python visualize_yolo_annotations.py path/to/image.jpg
  
  # Specify annotation file explicitly
  python visualize_yolo_annotations.py path/to/image.jpg --txt path/to/annotations.txt
  
  # With class names
  python visualize_yolo_annotations.py image.jpg --class-names 0:ball 1:hand
        """
    )
    
    parser.add_argument(
        'image',
        help='Path to the image file'
    )
    
    parser.add_argument(
        '--txt',
        help='Path to the annotation file (default: same name as image with .txt extension)'
    )
    
    parser.add_argument(
        '--class-names',
        nargs='+',
        help='Class names in format "id:name" (e.g., 0:ball 1:hand)'
    )
    
    args = parser.parse_args()
    
    # Parse class names if provided
    class_names = None
    if args.class_names:
        class_names = {}
        for item in args.class_names:
            try:
                class_id, name = item.split(':')
                class_names[int(class_id)] = name
            except ValueError:
                print(f"Warning: Invalid class name format '{item}', expected 'id:name'")
    
    # Visualize
    success = visualize_annotations(args.image, args.txt, class_names)
    
    if not success:
        sys.exit(1)


if __name__ == '__main__':
    main()