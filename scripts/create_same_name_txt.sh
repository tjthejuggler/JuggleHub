#!/bin/bash

# Script to create empty txt files with the same name as every jpg file in a directory
# Usage: ./create_same_name_txt.sh <directory>

# Check if directory argument is provided
if [ $# -eq 0 ]; then
    echo "Error: No directory specified"
    echo "Usage: $0 <directory>"
    exit 1
fi

# Get the directory from the first argument
DIR="$1"

# Check if the directory exists
if [ ! -d "$DIR" ]; then
    echo "Error: Directory '$DIR' does not exist"
    exit 1
fi

# Counter for created files
count=0

# Find all jpg files (case insensitive) and create corresponding txt files
for jpg_file in "$DIR"/*.jpg "$DIR"/*.JPG "$DIR"/*.jpeg "$DIR"/*.JPEG; do
    # Check if the file exists (handles case where no jpg files are found)
    if [ -f "$jpg_file" ]; then
        # Get the filename without extension
        base_name="${jpg_file%.*}"
        
        # Create empty txt file with the same base name
        txt_file="${base_name}.txt"
        
        # Only create if it doesn't already exist
        if [ ! -f "$txt_file" ]; then
            touch "$txt_file"
            echo "Created: $txt_file"
            ((count++))
        else
            echo "Skipped (already exists): $txt_file"
        fi
    fi
done

# Report results
if [ $count -eq 0 ]; then
    echo "No new txt files created (either no jpg files found or txt files already exist)"
else
    echo "Successfully created $count txt file(s)"
fi