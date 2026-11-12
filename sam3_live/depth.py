"""Depth utilities: box-median depth lookup, dropout handling, gating,
background rejection, overlap disambiguation and 3D lifting.

Rules of engagement (from the research brief):
- NEVER track on depth; SAM 3 is RGB-only — depth is only looked up.
- Small fast balls give stereo dropouts -> cache last-known depth per ID.
- Reject detections whose median box depth is outside the working range.
- When two track boxes overlap in 2D, prefer depth consistency to keep
  associations correct.
"""

import logging
from collections import deque
from typing import Dict, List, Optional

import numpy as np

from .types import Track

LOG = logging.getLogger(__name__)

# Fraction of pixels that must be valid inside a box to trust its median.
MIN_VALID_DEPTH_RATIO = 0.05


def box_median_depth(depth: np.ndarray, box) -> Optional[float]:
    """Median valid depth (meters) inside an xyxy box, or None on dropout."""
    h, w = depth.shape
    x1, y1 = max(0, int(box[0])), max(0, int(box[1]))
    x2, y2 = min(w - 1, int(box[2])), min(h - 1, int(box[3]))
    if x2 <= x1 or y2 <= y1:
        return None
    patch = depth[y1:y2 + 1, x1:x2 + 1].ravel()
    valid = patch[patch > 0]
    if valid.size < max(1, int(patch.size * MIN_VALID_DEPTH_RATIO)):
        return None  # stereo dropout
    med = float(np.median(valid))
    if not np.isfinite(med) or med <= 0:
        return None
    return med


class DepthContext:
    """Per-track depth history + working-range gating + 3D lift."""

    def __init__(self, working_range_m=(1.0, 4.0), history_len: int = 5):
        self.range_min, self.range_max = working_range_m
        self._history_len = history_len
        self._history: Dict[int, deque] = {}

    def _remember(self, track_id: int, depth_m: float) -> None:
        hist = self._history.setdefault(
            track_id, deque(maxlen=self._history_len)
        )
        hist.append(depth_m)

    def last_known(self, track_id: int) -> Optional[float]:
        hist = self._history.get(track_id)
        return hist[-1] if hist else None

    def apply(self, tracks: List[Track], depth: Optional[np.ndarray],
              intrinsics: Optional[dict]) -> List[Track]:
        """Gate by working range and attach depth + 3D position to tracks.

        Returns the filtered list. Tracks outside the working range are
        dropped (background clutter rejection); tracks with stereo
        dropout keep their last-known depth.
        """
        if depth is None:
            for t in tracks:
                t.depth_m = self.last_known(t.track_id)
                t.xyz = self._lift(t, intrinsics)
            return tracks

        kept: List[Track] = []
        for t in tracks:
            med = box_median_depth(depth, t.box)
            if med is not None and self.range_min <= med <= self.range_max:
                self._remember(t.track_id, med)
                t.depth_m = med
                t.xyz = self._lift(t, intrinsics)
                kept.append(t)
            elif med is not None:
                # Valid depth but out of range -> background/furniture.
                LOG.debug("reject track %d: depth %.2f outside [%g, %g]",
                          t.track_id, med, self.range_min, self.range_max)
            else:
                # Stereo dropout: keep only if we have last-known depth.
                last = self.last_known(t.track_id)
                if last is not None and self.range_min <= last <= self.range_max:
                    t.depth_m = last
                    t.xyz = self._lift(t, intrinsics)
                    kept.append(t)
                else:
                    LOG.debug("reject track %d: depth dropout, no history",
                              t.track_id)
        return kept

    def _lift(self, t: Track, intrinsics: Optional[dict]):
        """Deproject mask centroid to camera space (meters) via pinhole."""
        if t.depth_m is None or not intrinsics:
            return None
        fx, fy = intrinsics["fx"], intrinsics["fy"]
        cx, cy = intrinsics["cx"], intrinsics["cy"]
        u, v = t.centroid
        z = t.depth_m
        return ((u - cx) * z / fx, (v - cy) * z / fy, z)


def disambiguate_overlaps(tracks: List[Track], iou_threshold: float = 0.3) -> None:
    """Flag 2D-overlapping tracks so the overlay can mark them.

    Full ID-association repair across overlapping boxes is handled by
    SAM's own tracker; here we only annotate depth separation when two
    boxes overlap, which the CSV log exposes for offline evaluation of
    ID switches.
    """
    n = len(tracks)
    for i in range(n):
        for j in range(i + 1, n):
            a, b = tracks[i], tracks[j]
            ix1 = max(a.box[0], b.box[0]); iy1 = max(a.box[1], b.box[1])
            ix2 = min(a.box[2], b.box[2]); iy2 = min(a.box[3], b.box[3])
            if ix2 <= ix1 or iy2 <= iy1:
                continue
            inter = (ix2 - ix1) * (iy2 - iy1)
            union = a.width * a.height + b.width * b.height - inter
            if union > 0 and inter / union > iou_threshold:
                # Depth-separated overlaps are the interesting ones to audit.
                if a.depth_m is not None and b.depth_m is not None:
                    LOG.debug("overlap pair %d/%d IoU=%.2f dz=%.2fm",
                              a.track_id, b.track_id, inter / union,
                              abs(a.depth_m - b.depth_m))
