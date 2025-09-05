#!/usr/bin/env python3
"""
Script to add 'rs435i_' prefix to files and directories in a given directory.
Supports recursive operation with a flag.
"""

import os
import sys
import argparse
from pathlib import Path


def rename_items(directory_path, recursive=False, prefix="rs455_"):
    """
    Rename files and directories by adding a prefix.
    
    Args:
        directory_path (str): Path to the directory to process
        recursive (bool): Whether to process subdirectories recursively
        prefix (str): Prefix to add to file and directory names
    """
    directory = Path(directory_path)
    
    if not directory.exists():
        print(f"Error: Directory '{directory_path}' does not exist.")
        return False
    
    if not directory.is_dir():
        print(f"Error: '{directory_path}' is not a directory.")
        return False
    
    # Get all items in the directory
    items = list(directory.iterdir())
    
    # Sort items so directories come first (to handle renaming properly)
    items.sort(key=lambda x: (x.is_file(), x.name))
    
    renamed_count = 0
    
    for item in items:
        # Skip items that already have the prefix
        if item.name.startswith(prefix):
            print(f"Skipping '{item.name}' (already has prefix)")
            continue
        
        # Create new name with prefix
        new_name = prefix + item.name
        new_path = item.parent / new_name
        
        # Check if target already exists
        if new_path.exists():
            print(f"Warning: '{new_name}' already exists, skipping '{item.name}'")
            continue
        
        try:
            # Rename the item
            item.rename(new_path)
            print(f"Renamed: '{item.name}' -> '{new_name}'")
            renamed_count += 1
            
            # If recursive and this is a directory, process it
            if recursive and new_path.is_dir():
                print(f"Processing subdirectory: {new_path}")
                sub_renamed = rename_items(str(new_path), recursive=True, prefix=prefix)
                if sub_renamed:
                    renamed_count += sub_renamed
                    
        except OSError as e:
            print(f"Error renaming '{item.name}': {e}")
    
    return renamed_count


def main():
    parser = argparse.ArgumentParser(
        description="Add 'rs455_' prefix to files and directories in a given directory."
    )
    parser.add_argument(
        "directory",
        help="Directory path to process"
    )
    parser.add_argument(
        "-r", "--recursive",
        action="store_true",
        help="Process subdirectories recursively"
    )
    parser.add_argument(
        "--prefix",
        default="rs455_",
        help="Prefix to add (default: rs455_)"
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be renamed without actually renaming"
    )
    
    args = parser.parse_args()
    
    # Validate directory path
    directory_path = os.path.abspath(args.directory)
    
    print(f"Processing directory: {directory_path}")
    print(f"Prefix: '{args.prefix}'")
    print(f"Recursive: {args.recursive}")
    print(f"Dry run: {args.dry_run}")
    print("-" * 50)
    
    if args.dry_run:
        print("DRY RUN MODE - No files will be renamed")
        # For dry run, we'll simulate the process
        directory = Path(directory_path)
        if not directory.exists():
            print(f"Error: Directory '{directory_path}' does not exist.")
            return 1
        
        items = list(directory.iterdir())
        items.sort(key=lambda x: (x.is_file(), x.name))
        
        for item in items:
            if item.name.startswith(args.prefix):
                print(f"Would skip: '{item.name}' (already has prefix)")
            else:
                new_name = args.prefix + item.name
                print(f"Would rename: '{item.name}' -> '{new_name}'")
                
                if args.recursive and item.is_dir():
                    print(f"Would process subdirectory: {item}")
        
        return 0
    
    # Perform actual renaming
    try:
        renamed_count = rename_items(directory_path, args.recursive, args.prefix)
        print("-" * 50)
        print(f"Successfully renamed {renamed_count} items.")
        return 0
    except Exception as e:
        print(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())