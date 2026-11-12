"""Common data types for the sam3_live pipeline."""

from dataclasses import dataclass, field
from typing import Optional, Tuple


@dataclass
class Track:
    """One tracked object for one frame.

    box: (x1, y1, x2, y2) in pixels (xyxy).
    centroid: (cx, cy) pixels — mask centroid when available, else box center.
    mask_area: pixel count of the mask (0 if unknown).
    depth_m: median depth in meters at the box (None if unavailable/dropout).
    xyz: camera-space 3D position in meters (None without depth + intrinsics).
    """

    track_id: int
    box: Tuple[float, float, float, float]
    score: float
    centroid: Tuple[float, float] = (0.0, 0.0)
    mask_area: int = 0
    depth_m: Optional[float] = None
    xyz: Optional[Tuple[float, float, float]] = None
    timestamp: float = 0.0  # seconds since epoch of the source frame

    @property
    def width(self) -> float:
        return self.box[2] - self.box[0]

    @property
    def height(self) -> float:
        return self.box[3] - self.box[1]

    @property
    def box_center(self) -> Tuple[float, float]:
        x1, y1, x2, y2 = self.box
        return ((x1 + x2) / 2.0, (y1 + y2) / 2.0)


@dataclass
class FrameResult:
    """Everything the pipeline knows about one processed frame."""

    frame_index: int
    timestamp: float  # seconds since epoch
    tracks: list = field(default_factory=list)  # list[Track], already filtered
    inference_ms: float = 0.0
    source_fps: float = 0.0
