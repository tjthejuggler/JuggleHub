import os
import json
import shutil

def process_directory(v5_dir):
    v6_6_dir = v5_dir.replace("V9", "V9.1")
    if not os.path.exists(v6_6_dir):
        shutil.copytree(v5_dir, v6_6_dir)

    for root, _, files in os.walk(v6_6_dir):
        for file in files:
            if file.endswith(".json"):
                process_json_file(os.path.join(root, file))

def process_json_file(file_path):
    with open(file_path, 'r') as f:
        data = json.load(f)

    for image_id in data.get('_via_img_metadata', {}):
        image_data = data['_via_img_metadata'][image_id]
        for region in image_data.get('regions', []):
            shape_attributes = region.get('shape_attributes', {})
            if shape_attributes.get('name') == 'rect':
                x = shape_attributes.get('x', 0)
                y = shape_attributes.get('y', 0)
                width = shape_attributes.get('width', 0)
                height = shape_attributes.get('height', 0)

                # Grow by 5 pixels
                shape_attributes['x'] = x - 5
                shape_attributes['y'] = y - 5
                shape_attributes['width'] = width + 10
                shape_attributes['height'] = height + 10

    with open(file_path, 'w') as f:
        json.dump(data, f, indent=4)

if __name__ == "__main__":
    v5_directories = [
        "/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V9_targeted_lowish_light_60fps_default_realsense"
    ]

    for v5_dir in v5_directories:
        process_directory(v5_dir)