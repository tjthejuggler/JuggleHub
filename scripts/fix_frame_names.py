#!/usr/bin/env python3
"""
Script to rename frame files in the engine/data directory to include date/time format.

This script goes through all subdirectories in engine/data and renames files from:
  frame_X.jpg -> YYYY-MM-DD_HH-MM-SS_frame_X.jpg

Where YYYY-MM-DD_HH-MM-SS matches the directory name format.
"""

import os
import re
import sys
from pathlib import Path

def fix_frame_names(data_dir):
    """
    Fix frame names in all subdirectories of the data directory.
    
    Args:
        data_dir (str): Path to the data directory containing timestamped subdirectories
    """
    data_path = Path(data_dir)
    
    if not data_path.exists():
        print(f"Error: Data directory '{data_dir}' does not exist.")
        return False
    
    # Pattern to match timestamped directory names (YYYY-MM-DD_HH-MM-SS)
    timestamp_pattern = re.compile(r'^\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}$')
    
    # Pattern to match frame files that need renaming
    frame_pattern = re.compile(r'^frame_(\d+)\.jpg$')
    
    total_renamed = 0
    processed_dirs = 0
    
    # Iterate through all subdirectories
    for subdir in data_path.iterdir():
        if not subdir.is_dir():
            continue
            
        # Check if directory name matches timestamp format
        if not timestamp_pattern.match(subdir.name):
            print(f"Skipping directory '{subdir.name}' - doesn't match timestamp format")
            continue
            
        print(f"Processing directory: {subdir.name}")
        processed_dirs += 1
        renamed_in_dir = 0
        
        # Process all files in this directory
        for file_path in subdir.iterdir():
            if not file_path.is_file():
                continue
                
            # Check if file matches frame pattern
            match = frame_pattern.match(file_path.name)
            if not match:
                continue
                
            frame_number = match.group(1)
            
            # Create new filename with timestamp prefix
            new_filename = f"{subdir.name}_frame_{frame_number}.jpg"
            new_file_path = subdir / new_filename
            
            # Check if target file already exists
            if new_file_path.exists():
                print(f"  Skipping {file_path.name} - target {new_filename} already exists")
                continue
                
            try:
                # Rename the file
                file_path.rename(new_file_path)
                print(f"  Renamed: {file_path.name} -> {new_filename}")
                renamed_in_dir += 1
                total_renamed += 1
            except OSError as e:
                print(f"  Error renaming {file_path.name}: {e}")
        
        if renamed_in_dir > 0:
            print(f"  Renamed {renamed_in_dir} files in {subdir.name}")
        else:
            print(f"  No files to rename in {subdir.name}")
    
    print(f"\nSummary:")
    print(f"  Processed directories: {processed_dirs}")
    print(f"  Total files renamed: {total_renamed}")
    
    return True

def main():
    """Main function to run the script."""
    # Default data directory relative to script location
    script_dir = Path(__file__).parent
    default_data_dir = script_dir.parent / "engine" / "data"
    
    # Allow custom data directory as command line argument
    if len(sys.argv) > 1:
        data_dir = sys.argv[1]
    else:
        data_dir = str(default_data_dir)
    
    print(f"Fixing frame names in: {data_dir}")
    print("=" * 50)
    
    success = fix_frame_names(data_dir)
    
    if success:
        print("\nFrame name fixing completed successfully!")
    else:
        print("\nFrame name fixing failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()