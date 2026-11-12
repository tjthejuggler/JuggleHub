"""Per-frame track CSV logging + live stability stats.

CSV columns: frame, ts, id, x1, y1, x2, y2, score, mask_area,
depth_m, xyz (space-separated), plus inference_ms / n_tracks summary
rows are NOT written — one row per (frame, track).
"""

import csv
import math
import os
import time
from collections import defaultdict
from typing import Dict, List

from .types import FrameResult


class TrackCsvLogger:
    HEADER = ["frame", "ts", "id", "x1", "y1", "x2", "y2",
              "cx", "cy", "score", "mask_area", "depth_m",
              "xyz_x", "xyz_y", "xyz_z", "inference_ms"]

    def __init__(self, path: str):
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        self._file = open(path, "w", newline="")
        self._writer = csv.writer(self._file)
        self._writer.writerow(self.HEADER)
        self.path = path
        self.rows = 0

    def log_frame(self, result: FrameResult) -> None:
        for t in result.tracks:
            x = t.xyz if t.xyz else (math.nan,) * 3
            self._writer.writerow([
                result.frame_index, f"{result.timestamp:.6f}", t.track_id,
                f"{t.box[0]:.1f}", f"{t.box[1]:.1f}",
                f"{t.box[2]:.1f}", f"{t.box[3]:.1f}",
                f"{t.centroid[0]:.1f}", f"{t.centroid[1]:.1f}",
                f"{t.score:.3f}", t.mask_area,
                f"{t.depth_m:.3f}" if t.depth_m is not None else "",
                f"{x[0]:.3f}", f"{x[1]:.3f}", f"{x[2]:.3f}",
                f"{result.inference_ms:.1f}",
            ])
            self.rows += 1
        self._file.flush()

    def close(self) -> None:
        self._file.close()


class StabilityStats:
    """Live per-track stability for the HUD / final summary."""

    def __init__(self, expected_balls: int):
        self.expected = expected_balls
        self.frames = 0
        self.full_frames = 0
        self.first_seen: Dict[int, float] = {}
        self.last_seen: Dict[int, float] = {}
        self.hits: Dict[int, int] = defaultdict(int)
        self.total_id_switches = 0
        self._prev_ids = None
        self.started = time.time()

    def update(self, result: FrameResult) -> None:
        self.frames += 1
        ids = {t.track_id for t in result.tracks}
        if self.expected and len(ids) >= self.expected:
            self.full_frames += 1
        # ID-churn proxy: brand-new IDs appearing after tracking had
        # already reached the expected ball count.
        fresh = ids - self.first_seen.keys()
        if (self._prev_ids is not None and self.expected
                and len(self._prev_ids) >= self.expected):
            self.total_id_switches += len(fresh)
        for tid in ids:
            self.first_seen.setdefault(tid, result.timestamp)
            self.last_seen[tid] = result.timestamp
            self.hits[tid] += 1
        self._prev_ids = ids

    @property
    def coverage(self) -> float:
        return (self.full_frames / self.frames) if self.frames else 0.0

    def summary(self) -> str:
        lines = [f"frames={self.frames} "
                 f"coverage(>={self.expected} balls)={self.coverage:.1%} "
                 f"id-churn={self.total_id_switches}"]
        for tid in sorted(self.hits):
            dur = self.last_seen[tid] - self.first_seen[tid]
            lines.append(
                f"  id {tid}: {self.hits[tid]} frames over {dur:.1f}s"
            )
        return "\n".join(lines)
