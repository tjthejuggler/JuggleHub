import os
import argparse
import shutil
import re

def main():
    parser = argparse.ArgumentParser(description="Copy corresponding 'no_boxes' images to a new directory.")
    parser.add_argument("target_dir", help="The full path of the target directory containing 'boxes' images.")
    args = parser.parse_args()

    target_dir = args.target_dir
    if not os.path.isdir(target_dir):
        print(f"Error: Target directory not found at '{target_dir}'")
        return

    if not target_dir.endswith('_boxes'):
        print(f"Error: Target directory name must end with '_boxes'.")
        return
        
    # Create the output directory
    output_dir = target_dir.replace('_boxes', '_no_boxes')
    os.makedirs(output_dir, exist_ok=True)
    print(f"Created output directory: {output_dir}")

    # Base path for raw recordings
    base_raw_recordings_path = '/home/twain/Projects/JuggleHub/engine/data/1_raw_recordings'

    # Regex to extract info from filename
    # e.g. continuous_2025-09-27_09-21-45_frame_6577_boxes.jpg
    filename_pattern = re.compile(r'(.+)_frame_(\d+)_boxes\.jpg$')

    copied_count = 0
    not_found_count = 0

    # Iterate over files in the target directory
    for filename in os.listdir(target_dir):
        match = filename_pattern.match(filename)
        if match:
            recording_name = match.group(1)
            frame_number = match.group(2)

            # Construct the source filename (no_boxes version)
            source_filename = f"{recording_name}_frame_{frame_number}.jpg"

            # Construct the full source path
            source_path = os.path.join(base_raw_recordings_path, recording_name, 'no_boxes', source_filename)
            
            # Construct the destination path
            dest_path = os.path.join(output_dir, source_filename)

            if os.path.exists(source_path):
                shutil.copy(source_path, dest_path)
                print(f"Copied '{source_filename}'")
                copied_count += 1
            else:
                print(f"Warning: Corresponding 'no_boxes' image not found for '{filename}' at '{source_path}'")
                not_found_count += 1
        else:
            print(f"Warning: Filename '{filename}' does not match expected pattern. Skipping.")

    print("\nScript finished.")
    print(f"Successfully copied {copied_count} images.")
    print(f"Could not find {not_found_count} corresponding images.")

if __name__ == "__main__":
    main()