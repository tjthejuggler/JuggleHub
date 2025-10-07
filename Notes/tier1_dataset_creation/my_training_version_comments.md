2025-09-22 18:49:31
current bests(according to results):
yolo11n - V2_3_lonely_hands
yolo11s - V3_no_hands

V1 - there was some mislabeled data
V2_cleaned_more_blur - i fixed the mislabeled hands from V1 and i labelled some of the blurrier hands and balls
V2_1_empty_hands - an extra 200ish images of empty hands, no juggling balls
V2_2_lonely_hands - about 600 more images of empty hands, fake juggling, holding balls in weird angles and throws and catches
V2_3_lonely_hands - same as V2.2 but with slight changes to the augmented data code
V5 - specifically targeted edgecases images added, about 400 of them
V6 - all boxes made one pixel smaller on all sides compared to V5
V6.1 - all boxes made 2 pixels smaller on all sides compared to V5
V6.2 - all boxes made 1 pixel larger on all sides compared to V5
V6.6 - all boxes made 5 pixels smaller on all sides compared to V5
V8 - uses the bigger bounding boxes from V6.6
V9.1 - uses bigger bounding boxes from V6.6 and has more targeted failures as well as 2 kinds of augmented images based on the new targeted dataset
V9.1.1 - none of the V9 augmented data
V9.2 - same as V9.1 but has less augmented data('all' has been removed)
V9.3 - same as V9.1 except all 'ball' classes are 2 pixels smaller on all sides and all 'ball_held' are 1 pixels larger on all sides
