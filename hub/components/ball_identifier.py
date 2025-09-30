import logging
import cv2
import numpy as np
from components.color_profiler import ColorProfileManager

logger = logging.getLogger(__name__)

class BallIdentifier:
    """
    Assigns a persistent unique_id to ball detections based on color.
    """

    def __init__(self, color_profile_manager):
        self.color_profile_manager = color_profile_manager

    def identify_balls(self, raw_ball_detections, frame_image):
        """
        Identifies balls based on their color and assigns a unique_id.

        Args:
            raw_ball_detections (list): A list of detections, each with a bounding box.
            frame_image (np.ndarray): The image frame in BGR format.

        Returns:
            dict: A dictionary mapping detection index to unique_id.
        """
        logger.debug(f"Identifying {len(raw_ball_detections)} ball detections")
        
        if frame_image is None:
            logger.error("Frame image is None, cannot identify balls")
            return {}
        
        hsv_image = cv2.cvtColor(frame_image, cv2.COLOR_BGR2HSV)
        identified_balls = {}

        for i, detection in enumerate(raw_ball_detections):
            try:
                bbox = detection.bounding_box_2d
                x, y, w, h = int(bbox.x), int(bbox.y), int(bbox.width), int(bbox.height)
                logger.debug(f"Detection {i}: bbox=({x}, {y}, {w}, {h})")
                
                # Ensure the bounding box is within the image bounds
                x = max(0, x)
                y = max(0, y)
                w = min(w, frame_image.shape[1] - x)
                h = min(h, frame_image.shape[0] - y)

                ball_roi = hsv_image[y:y+h, x:x+w]
                
                if ball_roi.size == 0:
                    logger.warning(f"Detection {i}: ROI is empty, skipping")
                    continue

                # Calculate the average color
                avg_hsv_color = np.mean(ball_roi, axis=(0, 1))
                logger.debug(f"Detection {i}: avg_hsv_color={avg_hsv_color}")
                
                # Match the color to a profile
                unique_id = self.color_profile_manager.match_color(avg_hsv_color)
                
                if unique_id is None:
                    # If no match, create a new profile
                    unique_id = self.color_profile_manager.create_new_profile(avg_hsv_color)
                    self.color_profile_manager.save_profiles()
                    logger.info(f"Detection {i}: Created new color profile with unique_id={unique_id}")
                else:
                    logger.debug(f"Detection {i}: Matched to existing unique_id={unique_id}")
                
                identified_balls[i] = unique_id
            except Exception as e:
                logger.error(f"Error identifying detection {i}: {e}", exc_info=True)
            
        logger.debug(f"Identified {len(identified_balls)} balls: {identified_balls}")
        return identified_balls