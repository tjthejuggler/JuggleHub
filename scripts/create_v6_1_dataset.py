import os
import json
import shutil

def process_directory(v6_dir):
    v6_1_dir = v6_dir.replace("V6", "V6_1")
    if not os.path.exists(v6_1_dir):
        shutil.copytree(v6_dir, v6_1_dir)

    for root, _, files in os.walk(v6_1_dir):
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

                # Shrink by 1 more pixel
                shape_attributes['x'] = x + 1
                shape_attributes['y'] = y + 1
                shape_attributes['width'] = width - 2
                shape_attributes['height'] = height - 2

    with open(file_path, 'w') as f:
        json.dump(data, f, indent=4)

if __name__ == "__main__":
    v6_directories = [
        "/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V6rs455_normal_balls_mixedlight_sessions_intentional_realsense",
        "/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V6_intentional_edgecases_mixed_balls_normal_light_mixed_rs_no_boxes",
        "/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V6rs455_just_hands_low_light_intentional_and_auto_realsense",
        "/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V6_2_rs455_lonely_hands_low_light_intentional_realsense",
        "/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V6rs455_normal_balls_daylight_sessions_auto_realsense",
        "/home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V6rs455_led_balls_mixedlight_sessions_intentional_realsense"
    ]

    for v6_dir in v6_directories:
        process_directory(v6_dir)