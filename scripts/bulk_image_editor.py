import cv2
import numpy as np
import random
import os
import sys

# Global variables
ref_point = []
cropping = False
image_files = []
current_image_index = 0
original_image = None
display_image = None
selection_box = None

def load_image():
    """
    Loads the current image and prepares it for display.
    """
    global original_image, display_image, selection_box
    if 0 <= current_image_index < len(image_files):
        image_path = image_files[current_image_index]
        original_image = cv2.imread(image_path)
        display_image = original_image.copy()
        selection_box = None  # Reset selection on new image
        cv2.imshow("Image Editor", display_image)
        cv2.setWindowTitle("Image Editor", f"Image {current_image_index + 1}/{len(image_files)}: {os.path.basename(image_path)}")
    else:
        print("No more images.")


def show_image_with_selection():
    """Shows the image with the current selection box."""
    clone = display_image.copy()
    if selection_box:
        cv2.rectangle(clone, selection_box[0], selection_box[1], (0, 255, 0), 2)
    cv2.imshow("Image Editor", clone)

def click_and_crop(event, x, y, flags, param):
    """
    Mouse callback function to handle rectangle drawing.
    """
    global ref_point, cropping, display_image, selection_box

    if event == cv2.EVENT_LBUTTONDOWN:
        ref_point = [(x, y)]
        cropping = True

    elif event == cv2.EVENT_LBUTTONUP:
        ref_point.append((x, y))
        cropping = False
        # Ensure the rectangle has positive width and height
        x1, y1 = ref_point[0]
        x2, y2 = ref_point[1]
        selection_box = ((min(x1, x2), min(y1, y2)), (max(x1, x2), max(y1, y2)))
        show_image_with_selection()

    elif event == cv2.EVENT_MOUSEMOVE and cropping:
        clone = display_image.copy()
        cv2.rectangle(clone, ref_point[0], (x, y), (0, 255, 0), 2)
        cv2.imshow("Image Editor", clone)


def draw_random_shape(image, rect):
    """
    Draws a random shape over the specified rectangle in the image.
    """
    (x1, y1), (x2, y2) = rect
    if x1 > x2: x1, x2 = x2, x1
    if y1 > y2: y1, y2 = y2, y1

    center_x = (x1 + x2) // 2
    center_y = (y1 + y2) // 2
    width = abs(x2 - x1) * 2
    height = abs(y2 - y1) * 2

    # Slight variance in size
    size_variance = random.uniform(0.95, 1.05)
    width = int(width * size_variance)
    height = int(height * size_variance)

    color = (random.randint(0, 255), random.randint(0, 255), random.randint(0, 255))
    shape = random.choice(['square', 'triangle', 'diamond'])
    angle = random.randint(0, 360)

    if shape == 'square':
        pts = np.array([[-width//2, -height//2], [width//2, -height//2], [width//2, height//2], [-width//2, height//2]], np.int32)
    elif shape == 'triangle':
        pts = np.array([[0, -height//2], [-width//2, height//2], [width//2, height//2]], np.int32)
    elif shape == 'diamond':
        pts = np.array([[0, -height//2], [width//2, 0], [0, height//2], [-width//2, 0]], np.int32)

    # Rotate shape
    M = cv2.getRotationMatrix2D((0, 0), angle, 1)
    rotated_pts = cv2.transform(np.array([pts]), M)[0]
    
    # Translate shape to position
    rotated_pts += (center_x, center_y)

    cv2.fillPoly(image, [rotated_pts.astype(int)], color)


def apply_changes():
    """
    Applies the drawn shape to all selected images.
    """
    if selection_box and image_files:
        for image_path in image_files:
            img = cv2.imread(image_path)
            draw_random_shape(img, selection_box)
            cv2.imwrite(image_path, img)
        print(f"Applied changes to {len(image_files)} images.")


def find_images(directory):
    """Finds all image files in a directory."""
    supported_extensions = ('.jpg', '.jpeg', '.png', '.bmp', '.tiff')
    files = []
    for f in os.listdir(directory):
        if f.lower().endswith(supported_extensions):
            files.append(os.path.join(directory, f))
    return sorted(files)


def main():
    global image_files, current_image_index, display_image

    if len(sys.argv) != 2:
        print("Usage: python scripts/bulk_image_editor.py <path_to_image_directory>")
        print("Tip: Drag and drop the folder containing your images into the terminal after the script name.")
        return

    input_path = sys.argv[1]
    if not os.path.isdir(input_path):
        print(f"Error: Provided path '{input_path}' is not a directory.")
        return

    image_files = find_images(input_path)
    if not image_files:
        print(f"No images found in directory '{input_path}'.")
        return

    print("Welcome to the Bulk Image Editor!")
    print(f"Loaded {len(image_files)} images.")
    print("\nControls:")
    print("  Left/Right Arrow Keys: Cycle through images.")
    print("  Drag Mouse: Select an area to cover.")
    print("  'a': Apply the selected area as a random shape to ALL images and save.")
    print("  'r': Reset the selection on the current image.")
    print("  'q': Quit the program.")

    cv2.namedWindow("Image Editor")
    cv2.setMouseCallback("Image Editor", click_and_crop)
    
    load_image()

    while True:
        key = cv2.waitKey(1) & 0xFF

        if key == ord('q') or cv2.getWindowProperty("Image Editor", cv2.WND_PROP_VISIBLE) < 1:
            break
        elif key == ord('a'):
            if selection_box:
                apply_changes()
                print("Changes applied. Exiting.")
                break
            else:
                print("Please select an area first by dragging the mouse.")
        elif key == ord('r'):
             load_image() # effectively resets the view
        
        # Arrow key handling (may require checking for different key codes on some systems)
        if key == 81 or key == 2: # Left arrow for many systems
            if current_image_index > 0:
                current_image_index -= 1
                load_image()
        elif key == 83 or key == 3: # Right arrow for many systems
            if current_image_index < len(image_files) - 1:
                current_image_index += 1
                load_image()

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()