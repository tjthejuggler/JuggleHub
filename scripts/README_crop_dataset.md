# Dataset Cropping Script

## Overview

The [`crop_dataset_and_update_annotations.py`](crop_dataset_and_update_annotations.py) script crops YOLO dataset images and automatically updates the corresponding annotation files to maintain correct bounding box coordinates.

## Purpose

This script is designed to:
1. Crop images from 640x480 to 640x360 by removing pixels from top and bottom
2. Adjust YOLO annotation coordinates to match the new image dimensions
3. Create new versioned directories for the cropped datasets
4. Handle edge cases where objects are partially or completely outside the cropped area

## Usage

### Basic Command

```bash
python scripts/crop_dataset_and_update_annotations.py \
  --sessions SESSION_DIR1 SESSION_DIR2 ... \
  --old-version V8 \
  --new-version V10
```

### Full Example

```bash
python scripts/crop_dataset_and_update_annotations.py \
  --sessions V8rs455_lonely_hands_low_light_intentional_realsense \V8rs455_just_hands_low_light_intentional_and_auto_realsense \V8rs455_led_balls_mixedlight_sessions_intentional_realsense \V8rs455_normal_balls_daylight_sessions_auto_realsense \V8rs455_normal_balls_mixedlight_sessions_intentional_realsense \V8_intentional_edgecases_mixed_balls_normal_light_mixed_rs_no_boxes V8_ball_col_aug \V8_hard_negatives \
  V8_targeted_lowish_light_60fps_default_realsense \
  --old-version V8 \
  --new-version V10 \
  --original-height 480 \
  --crop-top 60 \
  --crop-bottom 60
```

## Parameters

### Required Parameters

- `--sessions`: List of annotation session directory names (space-separated)
  - These should be relative to the `engine/data/annotation_sessions` directory
  - Example: `V8_intentional_edgecases_mixed_balls_normal_light_mixed_rs_no_boxes`

- `--old-version`: Current version indicator in directory names
  - Example: `V8`

- `--new-version`: New version indicator for output directories
  - Example: `V10`

### Optional Parameters

- `--original-height`: Original image height in pixels (default: 480)
- `--crop-top`: Pixels to crop from top (default: 60)
- `--crop-bottom`: Pixels to crop from bottom (default: 60)
- `--base-dir`: Base directory containing annotation sessions (default: `engine/data/annotation_sessions`)

## How It Works

### Image Processing

1. Reads each `.jpg` or `.png` image from the `images/` subdirectory
2. Crops the specified number of pixels from top and bottom
3. Saves the cropped image to the new versioned directory

### Annotation Processing

**Important**: The script ensures every image has a corresponding `.txt` file, even if it contains no annotations (empty file).

For each annotation file (`.txt`), the script:

1. **Parses YOLO format**: Reads class ID, center X, center Y, width, and height (all normalized 0-1)

2. **Converts to pixel coordinates**: Transforms normalized coordinates to actual pixel positions

3. **Checks visibility**: Determines if the object is visible in the cropped area
   - Objects completely outside the crop are removed
   - Partially visible objects are clipped to the visible area

4. **Adjusts coordinates**: Recalculates center Y and height for the new image dimensions

5. **Normalizes**: Converts back to normalized coordinates (0-1) for the new image size

6. **Writes output**: Saves adjusted annotations to the new directory

### Mathematical Details

For a bounding box with:
- Original center Y: `cy_old` (normalized)
- Original height: `h_old` (normalized)
- Original image height: `H_orig` pixels
- Crop from top: `crop_top` pixels
- Crop from bottom: `crop_bottom` pixels

The new coordinates are calculated as:

```
# Convert to pixels
cy_px = cy_old * H_orig
h_px = h_old * H_orig

# Calculate box edges
top_px = cy_px - (h_px / 2)
bottom_px = cy_px + (h_px / 2)

# Clip to visible area
top_clipped = max(top_px, crop_top)
bottom_clipped = min(bottom_px, H_orig - crop_bottom)

# Adjust to new coordinate system
top_new = top_clipped - crop_top
bottom_new = bottom_clipped - crop_top

# Calculate new center and height
cy_new_px = (top_new + bottom_new) / 2
h_new_px = bottom_new - top_new

# Normalize to new image height
H_new = H_orig - crop_top - crop_bottom
cy_new = cy_new_px / H_new
h_new = h_new_px / H_new
```

## Output Structure

For each input session directory, the script creates a new directory with the version replaced:

```
Input:  engine/data/annotation_sessions/V8_session_name/images/
Output: engine/data/annotation_sessions/V10_session_name/images/
```

Each output directory contains:
- Cropped `.jpg` images
- Updated `.txt` annotation files (one for every image, even if empty)

## Safety Features

- **Overwrite protection**: Prompts before overwriting existing output directories
- **Error handling**: Continues processing even if individual files fail
- **Progress reporting**: Shows progress every 100 images
- **Validation**: Checks that base directory and session directories exist

## Example Output

```
Cropping dataset from 480px to 360px
Crop: 60px from top, 60px from bottom
Version: V8 → V10
Processing 2 session(s)...

Processing: V8_intentional_edgecases_mixed_balls_normal_light_mixed_rs_no_boxes → V10_intentional_edgecases_mixed_balls_normal_light_mixed_rs_no_boxes
  Processed 100/250 images...
  Processed 200/250 images...
  Completed: 250 images processed

Processing: V8rs455_normal_balls_daylight_sessions_auto_realsense → V10rs455_normal_balls_daylight_sessions_auto_realsense
  Processed 100/180 images...
  Completed: 180 images processed

Done! Total images processed: 430
```

## Dependencies

- Python 3.6+
- Pillow (PIL): `pip install Pillow`

## Notes

- The script only processes `.jpg` and `.png` image files
- **Every image gets a corresponding `.txt` file**, even if it has no annotations (empty file)
- X coordinates and widths remain unchanged (no horizontal cropping)
- Annotations for objects completely outside the cropped area are removed
- The script preserves the original YOLO format precision (6 decimal places)
- Empty `.txt` files are created for images that either:
  - Never had annotations in the original dataset
  - Had annotations that were completely cropped out

---

*Created: 2025-10-14*
*Last Updated: 2025-10-14*