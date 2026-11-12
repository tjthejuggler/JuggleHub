"""Recording session loader for engine/data/1_raw_recordings/<session>/.

Layout: no_boxes/frame_N.jpg (RGB), depth/frame_N_depth.png (aligned,
uint16 mm), optional recording.log with the engine's own event summary.
"""
from __future__ import annotations

import glob
import os
import re
from dataclasses import dataclass, field

import cv2
import numpy as np


@dataclass
class Frame:
    idx: int
    rgb: np.ndarray            # BGR
    depth: np.ndarray | None   # uint16 mm, aligned to rgb


class RecordingSession:
    def __init__(self, path: str, fps: float = 30.0, load_depth: bool = True):
        self.path = path
        self.name = os.path.basename(path.rstrip("/"))
        rgb_dir = os.path.join(path, "no_boxes")
        dep_dir = os.path.join(path, "depth")
        self._rgb_files = sorted(glob.glob(os.path.join(rgb_dir, "*.jpg")),
                                 key=_frame_key)
        self._depth_files = sorted(glob.glob(os.path.join(dep_dir, "*_depth.png")),
                                   key=_frame_key) if os.path.isdir(dep_dir) else []
        self.load_depth = load_depth and len(self._depth_files) == len(self._rgb_files)
        if not self._rgb_files:
            raise FileNotFoundError(f"no frames under {rgb_dir}")
        # effective fps from meta.json (wizard/record write actual capture rate)
        meta_file = os.path.join(path, "meta.json")
        if os.path.exists(meta_file):
            try:
                import json
                with open(meta_file) as f:
                    meta = json.load(f)
                stamps = meta.get("stamps") or []
                if len(stamps) > 10:
                    span = stamps[-1] - stamps[0]
                    if span > 0:
                        self.fps = (len(stamps) - 1) / span
                    elif meta.get("effective_fps"):
                        self.fps = float(meta["effective_fps"])
                elif meta.get("effective_fps"):
                    self.fps = float(meta["effective_fps"])
            except Exception:
                pass
            self.fps = max(5.0, min(self.fps, 60.0))

    def integrity(self) -> dict:
        """Repeated-colour-frame check (see camera.scan_recording). A session
        whose colour stream repeated frames while being recorded looks like a
        normal 30 fps recording by file count and by meta.json, but is not."""
        if getattr(self, "_integrity", None) is None:
            from .camera import scan_recording
            self._integrity = scan_recording(self._rgb_files)
        return self._integrity

    def __len__(self):
        return len(self._rgb_files)

    def __getitem__(self, i: int) -> Frame:
        rgb = cv2.imread(self._rgb_files[i])
        depth = None
        if self.load_depth:
            depth = cv2.imread(self._depth_files[i], cv2.IMREAD_ANYDEPTH)
        return Frame(idx=_frame_key(self._rgb_files[i]), rgb=rgb, depth=depth)

    def log_events(self) -> list:
        """Parse the engine's THROW/CATCH summary from recording.log."""
        events = []
        log = os.path.join(self.path, "recording.log")
        if not os.path.exists(log):
            return events
        pat = re.compile(r"^\s*(\d+)\s*\|\s*(THROW|CATCH)\s*\|\s*(.+?)\s*\|\s*(LEFT|RIGHT)")
        with open(log) as f:
            for line in f:
                m = pat.match(line)
                if m:
                    events.append(dict(frame=int(m.group(1)), event=m.group(2),
                                       ball=m.group(3), hand=m.group(4)))
        return events


def _frame_key(path: str) -> int:
    m = re.search(r"frame_(\d+)", os.path.basename(path))
    return int(m.group(1)) if m else 0
