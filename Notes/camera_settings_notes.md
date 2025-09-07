2025-09-06 21:03:30
no_blur.json, lights on, led balls, 30fps - there really is no blur, but everything is very dark and i dont think hand tracking would be possible(although maybe with the other camera, but this would require syncing up both cameras positions)

3exp_128gain, lights on, led balls, 30fps - still no blur, more background visible, still would probably fail hand tracking

12exp_128gain, lights on, led, 30fps - still no blur, can probably skeleton track, green and yellow very obvious

200exp_100gain, 30fps, lights on, led off - some blur on fast throws, but everything super visible

200exp_100gain, 60fps, lights on, led off - less blur on fast throws, but everything super visible

160exp_128gain, 60fps, lights on, led off - even less blur on fast throws, but everything super visible

159exp_128gain, 60fps, lights on, led off - almost completely black

50exp_128gain, 60fps, lights on, led off - almost completely black

30exp_128gain, 30fps, lights on, led off - no blur, skeleton should be fine, a little dark, but can see colors

40exp_128gain, 30fps, lights on, led off - almost no blur, skeleton should be fine, plenty of light, can see colors fine

60exp_128gain, 30fps, lights on, led off - almost triple length blur with highest speed throws, skeleton should be fine, plenty of light, can see colors fine

35exp_100gain, 30fps, lights on, led off - virtually no blur, skeleton should be fine, a little bit dark, can see colors fine


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