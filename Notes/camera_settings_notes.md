2025-09-06 21:03:30
no_blur.json, lights on, led balls - there really is no blur, but everything is very dark and i dont think hand tracking would be possible(although maybe with the other camera, but this would require syncing up both cameras positions)

3exp_128gain, lights on, led balls - still no blur, more background visible, still would probably fail hand tracking

12exp_128gain, lights on, led - still no blur, can probably skeleton track, green and yellow very obvious

200exp_100gain_60fps, lights on, led off - some blur on fast throws, but everything super visible

needs fixed:
025-09-06 21:13:45,819 - DEBUG - Received raw data: 136097 bytes
2025-09-06 21:13:45,819 - DEBUG - Parsed FrameData: frame_number=286, num_balls=2, num_raw_detections=10
DEBUG: Received frame_data with 2 balls.
DEBUG: Updating UI with frame_data containing 2 balls.
Traceback (most recent call last):
  File "/home/twain/Projects/JuggleHub/hub/components/ui.py", line 519, in _update_ui
    self.update_video_feed(frame_data)
    ~~~~~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^
  File "/home/twain/Projects/JuggleHub/hub/components/ui.py", line 615, in update_video_feed
    bbox = ball.bounding_box
           ^^^^^^^^^^^^^^^^^
AttributeError: bounding_box. Did you mean: 'bounding_box_2d'?
./scripts/run_hub.sh: line 293: 745517 Aborted           