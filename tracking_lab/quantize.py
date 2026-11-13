"""NNCF post-training INT8 quantization of the OpenVINO detectors.

Why: the live loop is CPU-inference bound (ball ~22 ms + pose ~24 ms). The
shower analysis (2026-11-13) showed per-catch physics fails at 12-17 fps
because the dip rhythm is aliased; ~2x faster nets -> ~30 fps processing,
which is above the aliasing limit and the foundation for pattern-agnostic
catch/throw identity (the long-term siteswap goal).

Accuracy is NEVER assumed: a quantized model changes detections, so it must
be re-judged (prox backtest over the labelled takes + the section 3 suite)
before anyone points TRACKING_BALL_MODEL at it.

Usage:
  python3 -m tracking_lab.quantize --model ball            # engine/models/yolo11n.xml
  python3 -m tracking_lab.quantize --model ball --xml path/to/other.xml
Output: .tmp/quant/<name>_int8.xml (+ .bin) next to it.
"""
from __future__ import annotations

import argparse
import os
import sys
import time

import cv2
import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO)

from tracking_lab.detectors import BALL_MODEL, POSE_MODEL, _letterbox  # noqa: E402
from tracking_lab.recording import RecordingSession                    # noqa: E402

CALIB_TAKES = [  # a spread of patterns/conditions; ~300 frames total
    "tracking_lab/recordings/2026-11-13_13-22-22_live50_t1",   # cascade
    "tracking_lab/recordings/2026-11-13_16-31-55_shower50_t1",  # shower
    "tracking_lab/recordings/2026-11-10_19-25-59_box30",       # older room/light
]
OUT_DIR = os.path.join(REPO, ".tmp", "quant")


def calibration_frames(takes: list[str], per_take: int) -> list[np.ndarray]:
    frames = []
    for take in takes:
        if not os.path.isdir(take):
            print(f"  (skipping missing calib take {take})")
            continue
        sess = RecordingSession(take, load_depth=False)
        step = max(1, len(sess) // per_take)
        for i in range(0, len(sess), step):
            frames.append(sess[i].rgb)
            if len([f for f in frames]) >= per_take * (takes.index(take) + 1):
                break
    return frames


def transform_for(xml: str):
    """Replicate the exact preprocessing each detector applies at runtime."""
    import openvino as ov
    m = ov.Core().read_model(xml)
    size = int(m.input(0).shape[-1])

    def transform(frame_bgr):
        canvas, _r, _t, _l = _letterbox(frame_bgr, size)
        return cv2.dnn.blobFromImage(canvas, 1 / 255.0, swapRB=True)

    return transform


def quantize(xml: str, out_xml: str, frames: list[np.ndarray]) -> None:
    import openvino as ov
    import nncf

    model = ov.Core().read_model(xml)
    dataset = nncf.Dataset(frames, transform_for(xml))
    print(f"  quantizing {os.path.basename(xml)} on {len(frames)} frames ...")
    t0 = time.time()
    quantized = nncf.quantize(
        model, dataset,
        subset_size=min(len(frames), 300),
        fast_bias_correction=True,
    )
    ov.save_model(quantized, out_xml)
    print(f"  -> {out_xml} ({time.time() - t0:.0f}s, "
          f"{os.path.getsize(out_xml.replace('.xml', '.bin')) / 1e6:.1f} MB)")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--model", choices=("ball", "pose"), default="ball")
    ap.add_argument("--xml", default=None, help="override source .xml")
    ap.add_argument("--frames", type=int, default=150,
                    help="calibration frames per take")
    a = ap.parse_args(argv)

    xml = a.xml or (BALL_MODEL if a.model == "ball" else POSE_MODEL)
    os.makedirs(OUT_DIR, exist_ok=True)
    base = os.path.splitext(os.path.basename(xml))[0]
    out_xml = os.path.join(OUT_DIR, f"{base}_int8.xml")

    frames = calibration_frames(CALIB_TAKES, a.frames)
    print(f"calibration: {len(frames)} frames from {len(CALIB_TAKES)} takes")
    quantize(xml, out_xml, frames)

    # quick smoke test: same API as the runtime detectors
    from tracking_lab.detectors import BallDetector
    det = BallDetector(xml=out_xml)
    out = det.detect(frames[0])
    print(f"smoke test: {len(out)} detections on a calibration frame")
    print(f"to use: TRACKING_BALL_MODEL={out_xml}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
