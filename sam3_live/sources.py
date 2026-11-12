"""Pluggable camera input sources.

Both sources yield (rgb_bgr, depth_or_None, timestamp) tuples where:
  - rgb_bgr is an HxWx3 uint8 array (BGR, OpenCV convention),
  - depth is an HxW float32 array in meters aligned to rgb, or None,
  - timestamp is time.time() at capture.
"""

import logging
import time
from typing import Optional, Tuple

import numpy as np

LOG = logging.getLogger(__name__)

Frame = Tuple[np.ndarray, Optional[np.ndarray], float]


class SourceError(RuntimeError):
    pass


def _try_import_cv2():
    try:
        import cv2

        return cv2
    except ImportError as exc:  # pragma: no cover
        raise SourceError("opencv-python is required for all sources") from exc


class WebcamSource:
    """Laptop/USB webcam via OpenCV VideoCapture. Default test baseline."""

    def __init__(self, device: int = 0, width: int = 1280, height: int = 720, fps: int = 30):
        self.cv2 = _try_import_cv2()
        self.cap = self.cv2.VideoCapture(device)
        if not self.cap.isOpened():
            raise SourceError(f"Cannot open webcam device {device}")
        self.cap.set(self.cv2.CAP_PROP_FRAME_WIDTH, width)
        self.cap.set(self.cv2.CAP_PROP_FRAME_HEIGHT, height)
        self.cap.set(self.cv2.CAP_PROP_FPS, fps)
        actual = (int(self.cap.get(self.cv2.CAP_PROP_FRAME_WIDTH)),
                  int(self.cap.get(self.cv2.CAP_PROP_FRAME_HEIGHT)))
        LOG.info("Webcam opened: device=%d resolution=%s", device, actual)
        self.intrinsics = None  # unknown without calibration

    def read(self) -> Frame:
        ok, frame = self.cap.read()
        if not ok:
            raise SourceError("Webcam read failed")
        return frame, None, time.time()

    def close(self) -> None:
        self.cap.release()


class RealSenseSource:
    """Intel RealSense (D435 etc.) via pyrealsense2 with depth aligned to RGB.

    Auto exposure is the default (changed 2026-11-12). The old default locked
    the colour sensor to ``exposure_us=2500`` on the assumption that the
    option is in microseconds. On this project's D455 the value 2000 was
    measured to cut the colour stream to ~5 unique frames/s (see
    tracking_lab/camera.py), which fits a 100 us unit, i.e. 2500 = 250 ms.
    That unit is inferred, not confirmed on a device, so ``exposure_us`` is
    passed through as a raw option value and is only applied when
    auto_exposure=False is requested explicitly.
    """

    def __init__(self, width: int = 1280, height: int = 720, fps: int = 30,
                 auto_exposure: bool = True, exposure_us: int = 2500):
        try:
            import pyrealsense2 as rs
        except ImportError as exc:  # pragma: no cover
            raise SourceError(
                "pyrealsense2 is required for --source realsense "
                "(pip install pyrealsense2)"
            ) from exc
        self.rs = rs
        self.width, self.height, self.fps = width, height, fps

        self.pipeline = rs.pipeline()
        config = rs.config()
        config.enable_stream(rs.stream.color, width, height, rs.format.bgr8, fps)
        config.enable_stream(rs.stream.depth, width, height, rs.format.z16, fps)
        try:
            self.profile = self.pipeline.start(config)
        except Exception as exc:
            raise SourceError(f"Cannot start RealSense pipeline: {exc}") from exc

        try:
            sensor = self.profile.get_device().first_color_sensor()
            if auto_exposure:
                # Set it explicitly: a manual value from an earlier run may
                # still be active on the sensor.
                sensor.set_option(rs.option.enable_auto_exposure, 1)
                LOG.info("RealSense color exposure: auto")
            else:
                sensor.set_option(rs.option.enable_auto_exposure, 0)
                sensor.set_option(rs.option.exposure, float(exposure_us))
                LOG.warning("RealSense color exposure locked to raw value %d "
                            "(unit unconfirmed; check the colour frame rate)",
                            exposure_us)
        except Exception as exc:
            LOG.warning("Could not set RealSense exposure (%s); the sensor "
                        "keeps whatever setting it had", exc)

        self.align = rs.align(rs.stream.color)
        self.depth_scale = (
            self.profile.get_device().first_depth_sensor().get_depth_scale()
        )
        self.intrinsics = self._color_intrinsics()
        LOG.info("RealSense opened: %dx%d@%d depth_scale=%.6f",
                 width, height, fps, self.depth_scale)

    def _color_intrinsics(self):
        stream = (self.profile.get_stream(rs.stream.color)
                  .as_video_stream_profile())
        intr = stream.get_intrinsics()
        return {
            "fx": intr.fx, "fy": intr.fy,
            "cx": intr.ppx, "cy": intr.ppy,
            "width": intr.width, "height": intr.height,
            "model": str(intr.model),
        }

    def read(self) -> Frame:
        rs = self.rs
        frames = self.pipeline.wait_for_frames(timeout_ms=5000)
        aligned = self.align.process(frames)
        color = aligned.get_color_frame()
        depth = aligned.get_depth_frame()
        if not color:
            raise SourceError("RealSense: no color frame")
        rgb = np.asanyarray(color.get_data())
        if depth:
            depth_m = (np.asanyarray(depth.get_data())
                       .astype(np.float32) * self.depth_scale)
        else:
            depth_m = None
        return rgb, depth_m, time.time()

    def close(self) -> None:
        try:
            self.pipeline.stop()
        except Exception:
            pass


def make_source(kind: str, **kwargs):
    if kind == "webcam":
        return WebcamSource(**kwargs)
    if kind == "realsense":
        return RealSenseSource(**kwargs)
    raise SourceError(f"Unknown source kind: {kind!r}")
