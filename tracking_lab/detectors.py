"""Ball + pose detectors (OpenVINO YOLO + MOG2 motion fusion, pose)."""
from __future__ import annotations

import os

import cv2
import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BALL_MODEL = os.path.join(REPO, "engine", "models", "yolo11n.xml")   # V8 640
# V11 (trained on our recordings incl. LED streaks; 2 classes 0=ball 1=held):
BALL_MODEL_V11 = os.path.join(REPO, "yolo11n_v11_openvino_model", "yolo11n_v11.xml")
# env override: TRACKING_MODEL_V11=1 uses the new model, otherwise V8
BALL_MODEL = (os.environ.get("TRACKING_MODEL_V11") == "1" and BALL_MODEL_V11) \
             or BALL_MODEL
# env override for any other model: TRACKING_BALL_MODEL=<path to an OpenVINO
# .xml>. Takes precedence over TRACKING_MODEL_V11. A wrong path is an error,
# so a typo can never silently fall back to V8 and be scored as the new model.
if os.environ.get("TRACKING_BALL_MODEL"):
    BALL_MODEL = os.path.abspath(os.environ["TRACKING_BALL_MODEL"])
    if not os.path.isfile(BALL_MODEL):
        raise FileNotFoundError(f"TRACKING_BALL_MODEL: no such file: {BALL_MODEL}")
POSE_MODEL = os.path.join(REPO, "engine", "models", "yolo11n-pose.xml")
# env override for pose model experiments (e.g. the 416 export). Same rule
# as TRACKING_DEVICE: a model change changes detections/wrists and must be
# re-judged (per-event marks + held-out), never adopted on speed alone.
if os.environ.get("TRACKING_POSE_MODEL"):
    POSE_MODEL = os.path.abspath(os.environ["TRACKING_POSE_MODEL"])
    if not os.path.isfile(POSE_MODEL):
        raise FileNotFoundError(
            f"TRACKING_POSE_MODEL: no such file: {POSE_MODEL}")
SKELETON_KPTS = {  # COCO
    "left_wrist": 9, "right_wrist": 10,
    "left_elbow": 7, "right_elbow": 8,
    "nose": 0,
}


def _first_output(result) -> np.ndarray:
    """Extract the single output tensor [C,N] from an infer() result dict."""
    arr = np.asarray(list(result.values())[0])
    if arr.ndim == 3:
        arr = arr[0]
    return arr


def _letterbox(frame: np.ndarray, size: int):
    h, w = frame.shape[:2]
    r = min(size / h, size / w)
    nh, nw = round(h * r), round(w * r)
    resized = cv2.resize(frame, (nw, nh), interpolation=cv2.INTER_LINEAR)
    canvas = np.full((size, size, 3), 114, np.uint8)
    top, left = (size - nh) // 2, (size - nw) // 2
    canvas[top:top + nh, left:left + nw] = resized
    return canvas, r, top, left


def _device() -> str:
    """OpenVINO device for the network detectors. Default CPU (the audited,
    scored configuration); TRACKING_DEVICE=GPU etc. for speed experiments —
    any change of device changes detections and must be re-judged via the
    §3 suite / per-event marks, never on speed alone."""
    return os.environ.get("TRACKING_DEVICE", "CPU")


class BallDetector:
    """yolo11n V8 (classes 0=ball, 1=ball_held), letterboxed 640."""

    def __init__(self, xml: str = BALL_MODEL, conf: float = 0.25, iou_nms: float = 0.45):
        import openvino as ov
        self.model = ov.Core().compile_model(xml, _device())
        self.infer = self.model.create_infer_request()
        self.size = int(self.model.input(0).shape[-1])
        self.conf = conf
        self.iou_nms = iou_nms
        self.detect(np.zeros((480, 640, 3), np.uint8))  # warm-up

    def detect(self, frame_bgr: np.ndarray):
        """Return [(box xyxy px, score, class)]."""
        h, w = frame_bgr.shape[:2]
        canvas, r, top, left = _letterbox(frame_bgr, self.size)
        blob = cv2.dnn.blobFromImage(canvas, 1 / 255.0, swapRB=True)
        pred = _first_output(self.infer.infer({0: blob}))     # [C,8400]
        boxes, scores_all = pred[:4].T, pred[4:]
        cls_ids = scores_all.argmax(axis=0)
        confs = scores_all.max(axis=0)
        keep = confs > self.conf
        boxes, confs, cls_ids = boxes[keep], confs[keep], cls_ids[keep]
        if len(boxes) == 0:
            return []
        xyxy = np.stack([boxes[:, 0] - boxes[:, 2] / 2, boxes[:, 1] - boxes[:, 3] / 2,
                         boxes[:, 0] + boxes[:, 2] / 2, boxes[:, 1] + boxes[:, 3] / 2], 1)
        idxs = cv2.dnn.NMSBoxes(xyxy.tolist(), confs.tolist(), self.conf, self.iou_nms)
        out = []
        for i in np.array(idxs).reshape(-1):
            x1, y1, x2, y2 = xyxy[i]
            out.append((((x1 - left) / r, (y1 - top) / r,
                         (x2 - left) / r, (y2 - top) / r),
                        float(confs[i]), int(cls_ids[i])))
        return out


class MotionDetector:
    """MOG2 background subtraction: finds moving blobs (catches motion-blurred
    balls YOLO misses on low-contrast scenes). Needs sequential frames."""

    def __init__(self, history: int = 150, var_threshold: float = 25.0):
        self.sub = cv2.createBackgroundSubtractorMOG2(
            history=history, varThreshold=var_threshold, detectShadows=False)
        self.kernel = np.ones((5, 5), np.uint8)
        self._warm = 0

    def detect(self, frame_bgr: np.ndarray) -> list:
        """Return [(box xyxy px, score, class=0)]."""
        self._warm += 1
        fg = self.sub.apply(frame_bgr)
        if self._warm < 30:      # model still learning the background
            return []
        fg = cv2.morphologyEx(fg, cv2.MORPH_OPEN, self.kernel)
        fg = cv2.morphologyEx(fg, cv2.MORPH_CLOSE, self.kernel)
        cnts, _ = cv2.findContours(fg, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        out = []
        for c in cnts:
            a = cv2.contourArea(c)
            if a < 150 or a > 5000:
                continue
            x, y, w, h = cv2.boundingRect(c)
            if not (0.5 < w / h < 2.0):
                continue
            out.append(((float(x), float(y), float(x + w), float(y + h)),
                        0.5, 0))
        return out


class PoseDetector:
    """yolo11n-pose: persons + COCO keypoints (wrists/elbows for hand logic)."""

    def __init__(self, xml: str = POSE_MODEL, conf: float = 0.35):
        import openvino as ov
        self.model = ov.Core().compile_model(xml, _device())
        self.infer = self.model.create_infer_request()
        self.size = int(self.model.input(0).shape[-1])
        self.conf = conf
        self.nkpt = (self.model.output(0).shape[1] - 5) // 3
        self.detect(np.zeros((480, 640, 3), np.uint8))

    def detect(self, frame_bgr: np.ndarray):
        """Return list of persons: {box, kpts: {name: (x,y,conf)}}."""
        h, w = frame_bgr.shape[:2]
        canvas, r, top, left = _letterbox(frame_bgr, self.size)
        blob = cv2.dnn.blobFromImage(canvas, 1 / 255.0, swapRB=True)
        pred = _first_output(self.infer.infer({0: blob}))   # [5+3K,8400]
        boxes, obj = pred[:4].T, pred[4]
        kpts = pred[5:].T  # [8400, 3K]
        keep = obj > self.conf
        idx = np.where(keep)[0]
        if len(idx) == 0:
            return []
        boxes_k, obj_k = boxes[idx], obj[idx]
        xyxy = np.stack([boxes_k[:, 0] - boxes_k[:, 2] / 2, boxes_k[:, 1] - boxes_k[:, 3] / 2,
                         boxes_k[:, 0] + boxes_k[:, 2] / 2, boxes_k[:, 1] + boxes_k[:, 3] / 2], 1)
        keep_nms = cv2.dnn.NMSBoxes(xyxy.tolist(), obj_k.tolist(), self.conf, 0.45)
        persons = []
        for i in idx[np.array(keep_nms).reshape(-1)]:
            cx, cy, bw, bh = boxes[i]
            box = ((cx - bw / 2 - left) / r, (cy - bh / 2 - top) / r,
                   (cx + bw / 2 - left) / r, (cy + bh / 2 - top) / r)
            k = kpts[i].reshape(self.nkpt, 3)
            kp = {}
            for name, idx in SKELETON_KPTS.items():
                if idx < self.nkpt and k[idx][2] > 0.3:
                    kp[name] = ((k[idx][0] - left) / r, (k[idx][1] - top) / r,
                                float(k[idx][2]))
            persons.append(dict(box=box, kpts=kp))
        return persons
