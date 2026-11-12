"""Per-frame perception shared by the offline runner and the detection cache.

Keeping this in ONE place guarantees that `python -m tracking_lab --rec ...`
and the cached replay used by `tracking_lab.evaluate` see identical inputs.
"""
from __future__ import annotations


def fuse(yolo_dets: list, motion_dets: list) -> list:
    """Union of YOLO + MOG2 blobs; a motion blob within 40 px of a YOLO
    detection is dropped as duplicate, the rest become low-confidence
    candidates the tracker's pending gate filters."""
    fused = list(yolo_dets)
    for b, s, c in motion_dets:
        if not any(abs((b[0] + b[2]) / 2 - (yb[0] + yb[2]) / 2) < 40
                   and abs((b[1] + b[3]) / 2 - (yb[1] + yb[3]) / 2) < 40
                   for yb, _s, _c in yolo_dets):
            fused.append((b, s, c))
    return fused


def perceive(rgb, det, pose, motion):
    """Run pose + ball + motion on one BGR frame.

    Returns (wrists {hand: (x, y)}, face_pt | None, dets_raw [(box, score, cls)],
    n_persons)."""
    persons = pose.detect(rgb)
    wrists = {}
    face_pt = None
    for p in persons:
        for name in ("left_wrist", "right_wrist"):
            if name in p["kpts"]:
                wrists[name] = (p["kpts"][name][0], p["kpts"][name][1])
        if face_pt is None and "nose" in p["kpts"]:
            face_pt = (p["kpts"]["nose"][0], p["kpts"]["nose"][1])
    dets_raw = fuse(det.detect(rgb), motion.detect(rgb))
    return wrists, face_pt, dets_raw, len(persons)


def make_depth_fn(depth, rgb_shape):
    """Closure (x, y) -> metres | None for an aligned uint16-mm depth image."""
    if depth is None:
        return None
    dh, dw = depth.shape[:2]
    sy, sx = rgb_shape[0] / dh, rgb_shape[1] / dw

    def depth_at(x, y):
        xi, yi = int(x / sx), int(y / sy)
        if 0 <= yi < dh and 0 <= xi < dw:
            mm = float(depth[yi, xi])
            return mm / 1000.0 if mm > 0 else None
        return None
    return depth_at
