"""Live overlay: boxes, per-ID trails, FPS readout.

Supports interpolated display: when inference is slower than the display
rate, the pipeline passes a fractional alpha in (0,1) and tracks are
extrapolated with constant velocity from their trail so the overlay
stays smooth between SAM result frames.
"""

from collections import deque
from typing import Dict, List

import numpy as np

from .types import Track

PALETTE = [
    (60, 200, 60), (60, 160, 255), (0, 200, 255), (0, 120, 255),
    (200, 120, 60), (220, 60, 200), (60, 220, 220), (160, 60, 220),
]
TRAIL_LEN = 60


class OverlayRenderer:
    def __init__(self, trail_len: int = TRAIL_LEN):
        import cv2
        self.cv2 = cv2
        self.trails: Dict[int, deque] = {}

    def _color(self, track_id: int):
        return PALETTE[track_id % len(PALETTE)]

    def _trail(self, track_id: int) -> deque:
        return self.trails.setdefault(track_id, deque(maxlen=TRAIL_LEN))

    def update_trails(self, tracks: List[Track]) -> None:
        for t in tracks:
            self._trail(t.track_id).append((t.centroid, t.timestamp))

    def _interp_centroid(self, t: Track, alpha: float):
        """Constant-velocity extrapolation between inference frames."""
        trail = self._trail(t.track_id)
        if alpha <= 0 or len(trail) < 2:
            return t.centroid
        (cx1, cy1, ts1), (cx0, cy0, ts0) = trail[-1], trail[-2]
        dt = max(ts1 - ts0, 1e-6)
        vx, vy = (cx1 - cx0) / dt, (cy1 - cy0) / dt
        lead = alpha * dt  # assume display gap ~ one inference interval
        return (cx1 + vx * lead, cy1 + vy * lead)

    def draw(self, frame: np.ndarray, tracks: List[Track],
             processed_fps: float, display_fps: float,
             interpolation_alpha: float = 0.0) -> np.ndarray:
        cv2 = self.cv2
        out = frame

        # Trails.
        for tid, trail in self.trails.items():
            if len(trail) < 2:
                continue
            pts = np.array([[int(x), int(y)] for (x, _y), _ts in trail],
                           dtype=np.int32)
            cv2.polylines(out, [pts], isClosed=False,
                          color=self._color(tid), thickness=2, lineType=cv2.LINE_AA)

        for t in tracks:
            color = self._color(t.track_id)
            cx, cy = (self._interp_centroid(t, interpolation_alpha)
                      if interpolation_alpha > 0 else t.centroid)
            x1, y1, x2, y2 = [int(v) for v in t.box]
            cv2.rectangle(out, (x1, y1), (x2, y2), color, 2)
            cv2.circle(out, (int(cx), int(cy)), 4, color, -1)
            label = f"#{t.track_id} {t.score:.2f}"
            if t.depth_m is not None:
                label += f" {t.depth_m:.2f}m"
            if t.xyz is not None:
                label += f" z={t.xyz[2]:.2f}"
            cv2.putText(out, label, (x1, max(14, y1 - 6)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1,
                        cv2.LINE_AA)

        hud = (f"SAM3-live  proc:{processed_fps:5.1f}FPS  "
               f"disp:{display_fps:5.1f}FPS  tracks:{len(tracks)}")
        cv2.rectangle(out, (0, 0), (520, 24), (0, 0, 0), -1)
        cv2.putText(out, hud, (8, 17), cv2.FONT_HERSHEY_SIMPLEX, 0.55,
                    (255, 255, 255), 1, cv2.LINE_AA)
        return out
