### Phase 1: Preparation and Camera Setup

**Objective:** Configure your tools and environment correctly before capturing a single frame.

**1. Identify Target Environments:** Make a list of every distinct lighting condition your system must work in.
    *   [ ] Bright, direct sunlight (outdoors)
    *   [ ] Overcast daylight / open shade (outdoors)
    *   [ ] Bright indoor office/lab (fluorescent lights)
    *   [ ] Typical indoor home (warm, incandescent/LED lights)
    *   [ ] Dimly lit room (dusk, single lamp)
    *   [ ] Challenging mixed lighting (e.g., bright window in a dark room)

**2. Physical Camera Setup:**
    *   **Use a Tripod:** For stability, especially when capturing the "sharp" dataset. This eliminates camera shake.
    *   **Clean the Lens:** A smudge can ruin hundreds of images. Use a microfiber cloth to clean the RGB sensor lens.

**3. RealSense Viewer Software Setup (for Data Capture):**
    *   Connect your D455 and open the RealSense Viewer.
    *   Enable the **Stereo Module** and **RGB Camera**.
    *   In the RGB Camera section, configure the following:
        *   **Turn OFF All Auto-Controls:**
            *   `Auto-Exposure Priority`: Toggle OFF
            *   `Backlight Compensation`: Set to 0
            *   `Auto White Balance`: Toggle OFF
            *   All other processing blocks (contrast, saturation, etc.) can be left at default.
        *   **Turn ON the IR Projector:** Find this control under the `Stereo Module` section. The projector is essential for getting good depth data on your ball during inference. You must train with it on.
        *   **Set Resolution/FPS:** A resolution of `1280x720` at `30 FPS` is a great starting point.
        *   **Set Format:** Use a format like `RGB8`.

You are now ready to capture controlled, high-quality data.

---

### Phase 2: The Data Collection Protocol

**Objective:** Systematically capture a diverse dataset covering all your target environments.

#### Part A: The Core Dataset (~85% of your effort) - Sharp & Varied Images

**For EACH environment on your list, perform the following steps:**

1.  **Establish Baseline "Nominal" Settings:**
    *   **Step 1 (Kill Motion Blur):** Set `Gain` to its minimum. Move the ball at its fastest expected speed. Lower the `Exposure` value until the ball appears perfectly sharp and frozen in the frame. **This is your most important setting.**
    *   **Step 2 (Set Visibility):** The image is likely dark now. Increase the `Gain` until the ball is clearly visible and well-defined.
    *   **Step 3 (Set White Balance):** Briefly toggle `Auto White Balance` ON, see the value it chooses (e.g., 4500), then toggle it OFF and manually set the slider to that value.

2.  **Capture Your Image Sets:**
    *   **[ ] Set 1: Nominal (40%):** Using the baseline settings you just found, capture many images of the ball in different positions and orientations.
    *   **[ ] Set 2: Underexposed (20%):** Lower the `Gain` or `Exposure` slightly from your nominal settings. The image should be darker but the ball still perceptible. Capture many images.
    *   **[ ] Set 3: Overexposed (20%):** Increase the `Gain` or `Exposure` from nominal. The image should be brighter, perhaps slightly washed out. Capture many images.
    *   **[ ] Set 4: Color Cast (5%):** Return to nominal brightness settings. Deliberately set an incorrect `White Balance` (e.g., set it to 2800 in a daylight scene to make it look blue, or 6500 in an office to make it look yellow). Capture a smaller set of images.

#### Part B: The Supplemental Dataset (~15% of your effort) - Realistic Motion Blur

**For a few representative environments (e.g., one bright, one dim), perform these steps:**

1.  **Establish "Blurry" Settings:**
    *   Set a **slower shutter speed** (a higher `Exposure` value, e.g., 16666µs for 1/60s).
    *   Adjust `Gain` as needed for visibility.
2.  **[ ] Capture Blurry Images:**
    *   Keep the camera stationary on the tripod.
    *   Move the ball at realistic speeds across the frame.
    *   Capture images that show clear object motion blur against a sharp background.

---

### Phase 3: The Labeling Strategy

**Objective:** Create a perfectly consistent and accurate "answer key" for your model.

*   **For all SHARP images:** Label with a **tight, pixel-perfect** bounding box. Be meticulous.
*   **For all BLURRY images:** Choose **one** consistent method and stick to it.
    *   **Recommended Method:** Label the "ghost" of the object. Draw the bounding box where the ball's center of mass is within the blur streak, making the box the correct size of the ball itself. This better represents the object's true location.

---

### Phase 4: Training with Custom Object-Specific Blur Augmentation

**Objective:** Teach your model to recognize a blurred ball in a sharp environment by augmenting your perfect, sharp data during training.

**1. The Custom Augmentation Code:**
Below is an improved and more robust Python function using OpenCV and NumPy. This code creates a randomized linear motion blur and applies it only to the object.

```python
import cv2
import numpy as np
import random

def apply_object_motion_blur(image, bboxes, blur_limit=(3, 15), p=0.5):
    """
    Applies motion blur to objects within their bounding boxes.

    Args:
        image (np.array): The input BGR image.
        bboxes (list of lists): A list of bounding boxes, e.g., [[x_min, y_min, x_max, y_max], ...].
        blur_limit (tuple): A range (min, max) for the blur kernel size.
        p (float): The probability of applying the augmentation.

    Returns:
        np.array: The image with potential object motion blur.
    """
    if random.random() > p:
        return image

    output_image = image.copy()

    for bbox in bboxes:
        x_min, y_min, x_max, y_max = [int(v) for v in bbox]
        
        # Ensure the bounding box has a valid area
        if x_min >= x_max or y_min >= y_max:
            continue

        # 1. Crop the object from the image
        object_crop = output_image[y_min:y_max, x_min:x_max]
        
        # 2. Create a randomized motion blur kernel
        kernel_size = random.randint(blur_limit[0], blur_limit[1])
        # Ensure kernel size is odd
        if kernel_size % 2 == 0:
            kernel_size += 1
            
        kernel = np.zeros((kernel_size, kernel_size))
        
        # Randomly choose horizontal or vertical blur
        if random.random() > 0.5:
            # Horizontal blur
            center = int((kernel_size - 1) / 2)
            kernel[center, :] = np.ones(kernel_size)
        else:
            # Vertical blur
            center = int((kernel_size - 1) / 2)
            kernel[:, center] = np.ones(kernel_size)
            
        kernel = kernel / kernel_size # Normalize the kernel
        
        # 3. Apply the blur to the cropped object
        blurred_crop = cv2.filter2D(object_crop, -1, kernel)
        
        # 4. Paste the blurred object back onto the image
        output_image[y_min:y_max, x_min:x_max] = blurred_crop
        
    return output_image
```

**2. How to Integrate into Your Training Pipeline:**
You will need to insert this function into your data loading process. Most YOLO pipelines are built on PyTorch, so here is a conceptual example of how it would fit inside a PyTorch `Dataset` class.

```python
# --- This is a conceptual example ---
from torch.utils.data import Dataset
import cv2

class YOLODataset(Dataset):
    def __init__(self, image_paths, label_paths, augmentations=None):
        self.image_paths = image_paths
        self.label_paths = label_paths
        self.augmentations = augmentations # Your standard augmentations (brightness, etc.)

    def __len__(self):
        return len(self.image_paths)

    def __getitem__(self, index):
        # 1. Load image and labels
        image = cv2.imread(self.image_paths[index])
        # ... code to load your labels (bboxes) for this image ...
        labels = load_labels(self.label_paths[index])
        bboxes = labels_to_bboxes(labels, image.shape) # convert to [xmin, ymin, xmax, ymax]

        # 2. Apply standard augmentations FIRST (flips, rotations, etc.)
        if self.augmentations:
            # ... apply standard augmentations and update bboxes accordingly ...
            pass
            
        # 3. Apply our custom object blur augmentation LAST
        # We do it last to ensure the blur direction isn't weirdly rotated.
        image = apply_object_motion_blur(image, bboxes, p=0.6) # Apply 60% of the time

        # 4. Convert image to tensor and return
        # ... code to convert image to tensor, format labels for YOLO ...
        
        return image_tensor, formatted_labels
```

This ensures that for a majority of training iterations, your model sees a perfectly sharp ball pasted onto a sharp background get transformed into a realistically blurry ball on that same sharp background. It learns exactly the pattern you want it to.