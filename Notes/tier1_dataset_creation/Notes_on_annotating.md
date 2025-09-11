2025-09-11 13:41:08
i think these notes are outdated now

# Number of Balls

Why Multi-Ball Images are Critical

Including images with 2, 3, or even more balls in a single frame is essential for several reasons:

    Learning Context: It teaches the model what a "scene with juggling balls" looks like. It learns to not be surprised or confused by the presence of multiple targets. If it only ever sees one ball at a time during training, it might be less confident when it suddenly sees three in the live application.

    Improving Non-Maximum Suppression (NMS): The NMS step is what prevents the model from detecting the same ball multiple times with overlapping boxes. By training on images where balls are close together, the model gets better at distinguishing between "one ball detected twice" and "two distinct but nearby balls." This will directly help the scenario you described earlier, where setting NMS too high caused one ball to be seen as many. Training on multi-ball images helps the model produce cleaner detections that require less aggressive NMS settings.

    Preventing Environmental Bias: If you only ever train on single-ball images, the model might inadvertently learn biases from the background. It might start to associate a certain texture on your wall with the presence of a ball. By having multiple balls in different positions, you force the model to focus on the features of the balls themselves, as they are the only consistent objects across the varied scenes.

Actionable Advice for Your Data Collection

When you are recording your clips and then selecting frames for your V1 dataset, make sure you have a healthy mix of scenarios in your ~300 images:

    ~25% Single Ball Images: These are still very important. Include shots of a single ball held stationary, a single ball at the peak of its arc (where it's nearly stationary), and a single ball in motion (with some motion blur). This helps the model learn the core appearance of the ball in isolation.

    ~75% Multi-Ball Images: The majority of your dataset should reflect the real-world use case. Capture frames from your standard three-ball cascade and any other patterns you practice.

        Prioritize interesting moments:

            Frames where the balls are crossing paths in the air.

            Frames where one ball is in your hand while others are in the air (occlusion practice!).

            Frames where the balls are close together at the peak of a pattern.

By ensuring your dataset is a realistic representation of a juggling session, you are giving the model the exact information it needs to perform robustly in that environment. You are on exactly the right track.

# Bounding Boxes

The Rule: Always Use an Upright Rectangle

You must always use a standard, upright rectangular bounding box, even if it's not a perfect fit and includes some empty space.

Here's why this is a non-negotiable rule for this type of object detection model (YOLO and its cousins):

    Model Architecture: The fundamental output of the YOLO model is a set of (x, y, width, height) coordinates. This format, by its very definition, describes an upright rectangle. The model is architecturally incapable of predicting a rotated ellipse or a diagonal box. It can only "think" in terms of axis-aligned rectangles.

    Training Consistency: If you were to somehow provide a different shape (like a rotated rectangle), the training process would fail because the format of your label would not match the format of the model's output. The training code calculates the "loss" (the error) by comparing the predicted upright rectangle to the labeled upright rectangle. It has no way to compare an upright rectangle to a rotated one.

How to Handle Motion Blur (The "Do This / Don't Do This" Guide)

When you encounter a frame where the ball is blurred into an oval or a streak, here is the correct way to annotate it:

DO THIS:

    Draw an upright rectangular bounding box that tightly encloses the entire blurred streak.

    The goal is to capture all the pixels that belong to the moving object.

    This will naturally include some empty space in the corners of the box. This is expected and correct.

DON'T DO THIS:

    Do not try to draw the box diagonally to match the angle of the streak.

    Do not make the box extra large "just in case." Keep it as tight as possible to the visible ends of the blur.

    Do not worry about the empty space in the corners.

Why This Works: Teaching the Model About Motion

By labeling the entire streak, you are teaching the model a valuable lesson: "Objects that look like this blurred oval are also valid instances of a 'ball'."

The model will learn to recognize the "motion blur" texture as a feature associated with your target object. This makes it more robust, not less. When it sees a fast-moving ball in the live application, it won't be confused by the blur. Instead, it will correctly identify it because it has seen many examples of "blurry balls" in its training data and knows they should be labeled.

This is a form of implicit data augmentation. You are showing the model what the object looks like under a wider variety of conditions, which is a cornerstone of building a powerful and reliable detector.