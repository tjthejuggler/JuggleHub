# YOLO Annotation Visualization Script

## Overview

The [`visualize_yolo_annotations.py`](visualize_yolo_annotations.py) script displays images with their YOLO bounding box annotations overlaid. This is useful for verifying that annotations are correct, especially after dataset transformations like cropping.

## Purpose

- Verify that bounding boxes are correctly positioned after dataset operations
- Visually inspect annotation quality
- Debug annotation issues
- Quick visual confirmation of dataset integrity

## Installation

The script requires matplotlib:

```bash
pip install matplotlib pillow
```

## Usage

### Basic Usage

Display an image with its annotations (automatically looks for matching `.txt` file):

```bash
python scripts/visualize_yolo_annotations.py /home/twain/Projects/JuggleHub/engine/data/annotation_sessions/V10rs455_normal_balls_mixedlight_sessions_intentional_realsense/images/rs455_2025-09-15_12-02-05_frame_88.jpg
```

### Specify Annotation File

If the annotation file has a different name or location:

```bash
python scripts/visualize_yolo_annotations.py path/to/image.jpg --txt path/to/annotations.txt
```

### With Class Names

Add human-readable class labels:

```bash
python scripts/visualize_yolo_annotations.py image.jpg --class-names 0:ball 1:hand
```

## Examples

### Example 1: Verify Cropped Dataset

After running the crop script, verify a cropped image:

```bash
python scripts/visualize_yolo_annotations.py \
  engine/data/annotation_sessions/V10_intentional_edgecases_mixed_balls_normal_light_mixed_rs_no_boxes/images/continuous_2025-09-27_09-17-14_frame_1901.jpg
```

### Example 2: Compare Original vs Cropped

Verify original image:
```bash
python scripts/visualize_yolo_annotations.py \
  engine/data/annotation_sessions/V8_intentional_edgecases_mixed_balls_normal_light_mixed_rs_no_boxes/images/continuous_2025-09-27_09-17-14_frame_1901.jpg \
  --class-names 1:ball
```

Then verify cropped version:
```bash
python scripts/visualize_yolo_annotations.py \
  engine/data/annotation_sessions/V10_intentional_edgecases_mixed_balls_normal_light_mixed_rs_no_boxes/images/continuous_2025-09-27_09-17-14_frame_1901.jpg \
  --class-names 1:ball
```

### Example 3: Check Empty Annotations

Verify images with no annotations (should show image with no boxes):

```bash
python scripts/visualize_yolo_annotations.py \
  engine/data/annotation_sessions/V10_session/images/some_image.jpg
```

## Features

### Visual Elements

- **Bounding Boxes**: Colored rectangles showing object locations
- **Class Labels**: Text labels showing class ID and name (if provided)
- **Image Info**: Title showing filename, dimensions, and annotation count
- **Color Coding**: Different colors for different classes

### Color Scheme

Classes are automatically assigned colors in rotation:
- Class 0: Red
- Class 1: Blue
- Class 2: Green
- Class 3: Yellow
- Class 4: Cyan
- Class 5: Magenta
- (repeats for higher class IDs)

### Information Display

The title bar shows:
- Image filename
- Image dimensions (width x height)
- Number of annotations found
- Status messages:
  - "(no annotation file found)" - if `.txt` file doesn't exist
  - "(empty annotation file)" - if `.txt` file exists but is empty

## Verification Workflow

### After Cropping Dataset

1. **Pick representative images** from different sessions
2. **Visualize original** to see baseline
3. **Visualize cropped** to verify transformations
4. **Check edge cases**:
   - Images with objects near top/bottom edges
   - Images with multiple objects
   - Images with no annotations

### What to Look For

✅ **Correct**:
- Bounding boxes tightly fit objects
- No boxes extending outside image boundaries
- Objects near crop boundaries are properly clipped
- Empty annotation files for images with no visible objects

❌ **Issues to Watch For**:
- Boxes in wrong positions
- Boxes extending beyond image edges
- Missing boxes for visible objects
- Boxes for objects that should have been cropped out

## Command-Line Arguments

### Required

- `image`: Path to the image file to visualize

### Optional

- `--txt`: Path to annotation file (default: looks for `.txt` with same name as image)
- `--class-names`: Space-separated list of class mappings in format `id:name`
  - Example: `--class-names 0:ball 1:hand 2:box`

## Output

The script opens a matplotlib window displaying:
- The image
- Bounding boxes overlaid in color
- Class labels for each box
- Image information in the title

Close the window to exit.

## Technical Details

### Coordinate Conversion

The script converts YOLO normalized coordinates to pixel coordinates:

```
YOLO format: class_id center_x center_y width height (all 0-1)
Pixel format: x1, y1, x2, y2 (absolute pixels)

Conversion:
center_x_px = center_x * image_width
center_y_px = center_y * image_height
width_px = width * image_width
height_px = height * image_height

x1 = center_x_px - (width_px / 2)
y1 = center_y_px - (height_px / 2)
x2 = center_x_px + (width_px / 2)
y2 = center_y_px + (height_px / 2)
```

### Supported Formats

- **Images**: `.jpg`, `.png`, and other formats supported by PIL
- **Annotations**: YOLO format `.txt` files

## Tips

1. **Batch Verification**: Create a shell script to visualize multiple images:
   ```bash
   for img in engine/data/annotation_sessions/V10_*/images/*.jpg; do
       python scripts/visualize_yolo_annotations.py "$img"
   done
   ```

2. **Quick Spot Check**: Visualize a few random images from each session to verify the entire dataset

3. **Edge Cases First**: Always check images with objects near the crop boundaries

4. **Compare Dimensions**: Note the image dimensions in the title - cropped images should be 640x360 vs original 640x480

---

*Created: 2025-10-15*
*Last Updated: 2025-10-15*