import json
import os
import argparse
from PIL import Image

def convert_via_to_yolo(via_json_paths, class_list_path):
    """
    Converts VIA (VGG Image Annotator) JSON project files to YOLOv5 format.

    This script can process multiple VIA JSON files at once, finds the associated
    image files, and generates a corresponding YOLO format .txt file for each
    annotated image.

    Args:
        via_json_paths (list): A list of paths to the VIA JSON project files.
        class_list_path (str): Path to a text file containing the list of class
                               names, one per line. The line number corresponds
                               to the class ID (line 1 = ID 0).
    """
    # --- 1. Load the Class List ---
    try:
        with open(class_list_path, 'r') as f:
            class_list = [line.strip() for line in f.readlines()]
        print(f"Successfully loaded {len(class_list)} classes: {class_list}")
    except FileNotFoundError:
        print(f"FATAL: Class list file not found at '{class_list_path}'")
        return

    # --- 2. Process Each VIA Project File ---
    for via_json_path in via_json_paths:
        print(f"\nProcessing VIA project file: {via_json_path}...")
        
        try:
            with open(via_json_path, 'r') as f:
                via_data = json.load(f)
        except FileNotFoundError:
            print(f"  WARNING: JSON file not found. Skipping.")
            continue
        except json.JSONDecodeError:
            print(f"  WARNING: Invalid JSON in file. Skipping.")
            continue

        # The actual image metadata is in the '_via_img_metadata' key
        image_metadata = via_data.get('_via_img_metadata', {})
        if not image_metadata:
            print("  WARNING: No image metadata found in this JSON file. Skipping.")
            continue

        # Determine the base directory for images from the JSON file's location
        # Assumes images are in an 'images' subdirectory relative to the JSON file.
        json_dir = os.path.dirname(via_json_path)

        # --- 3. Iterate Through Each Image in the Project ---
        for image_id, data in image_metadata.items():
            filename = data['filename']
            image_path = os.path.join(json_dir, 'images', filename)
            
            if not os.path.exists(image_path):
                print(f"  WARNING: Image file not found for '{filename}'. Skipping.")
                continue

            # We must open the image to get its dimensions for normalization
            try:
                with Image.open(image_path) as img:
                    img_width, img_height = img.size
            except IOError:
                print(f"  WARNING: Could not open image '{filename}'. Skipping.")
                continue

            yolo_annotations = []
            
            # --- 4. Process Each Annotation (Region) for the Image ---
            for region in data.get('regions', []):
                region_attrs = region.get('region_attributes', {})
                # The attribute we defined in VIA was 'name'
                label = region_attrs.get('name')

                if not label:
                    print(f"  WARNING: Found a region without a 'name' attribute in '{filename}'. Skipping region.")
                    continue
                
                if label not in class_list:
                    print(f"  WARNING: Label '{label}' not found in class list. Skipping region.")
                    continue

                class_id = class_list.index(label)
                
                shape_attrs = region.get('shape_attributes', {})
                if shape_attrs.get('name') != 'rect':
                    print(f"  WARNING: Annotation is not a rectangle in '{filename}'. Skipping region.")
                    continue

                x = shape_attrs['x']
                y = shape_attrs['y']
                w = shape_attrs['width']
                h = shape_attrs['height']

                # --- 5. Convert to YOLO Format ---
                # YOLO format requires normalized coordinates:
                # x_center, y_center, width, height
                x_center_norm = (x + w / 2) / img_width
                y_center_norm = (y + h / 2) / img_height
                width_norm = w / img_width
                height_norm = h / img_height

                yolo_annotations.append(
                    f"{class_id} {x_center_norm:.6f} {y_center_norm:.6f} {width_norm:.6f} {height_norm:.6f}"
                )

            # --- 6. Write the YOLO .txt File ---
            if yolo_annotations:
                # The output file should be in the same directory as the image
                output_path = os.path.splitext(image_path)[0] + '.txt'
                
                with open(output_path, 'w') as f:
                    f.write('\n'.join(yolo_annotations))
                print(f"  -> Generated {len(yolo_annotations)} annotations for {filename}")

    print("\nConversion complete!")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Convert VIA (VGG Image Annotator) JSON files to YOLOv5 format.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        'via_json_paths',
        metavar='VIA_JSON_PATH',
        type=str,
        nargs='+',
        help="Path(s) to one or more VIA project JSON files."
    )
    parser.add_argument(
        '--class_list',
        type=str,
        default='/home/twain/Projects/JuggleHub/engine/data/classes.txt',
        help="Path to the file containing class names, one per line. \n(default: data/classes.txt)"
    )

    args = parser.parse_args()
    convert_via_to_yolo(args.via_json_paths, args.class_list)
