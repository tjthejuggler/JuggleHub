"""Pluggable ball trackers.

- Sam3Tracker: Meta SAM 3 / SAM 3.1 official predictor (facebookresearch/sam3)
  driven by a text prompt, running live via sliding-window sessions over an
  in-memory PIL frame buffer. Loads a LOCAL checkpoint — no HuggingFace
  authentication or gated license form is needed.
- LegacyColorTracker: HSV color-blob centroid tracker kept for A/B
  comparison with the old phone-era approach.

SAM 3 live adaptation notes:
  The OSS predictor's handle_request API is session-based over a fixed
  "video" (resource_path may be a list of PIL Images). For a live camera
  we run sliding-window sessions: frames accumulate in a buffer; every
  `window` frames we start a session on the buffer, add the text prompt
  once, propagate, and consume per-frame obj_ids / boxes / masks. Across
  windows, final boxes are re-fed as box prompts (with obj_ids) on the
  first frame so identities carry over. Streaming-style hotstart dedup
  heuristics do not run in this mode, so the pipeline filters duplicates
  (score threshold + expected ball count).
"""

import logging
import os
import time
from typing import Dict, List, Optional

import numpy as np

from .types import Track

LOG = logging.getLogger(__name__)

DEFAULT_CHECKPOINT = os.environ.get(
    "SAM3_CHECKPOINT",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                 "models", "sam3", "sam3.pt"),
)


class Sam3Tracker:
    """SAM 3 video tracker driven by a text prompt (local checkpoint)."""

    name = "sam3"

    def __init__(self, prompt: str = "ball",
                 checkpoint_path: str = DEFAULT_CHECKPOINT,
                 version: str = "sam3",  # "sam3" | "sam3.1"
                 score_threshold: float = 0.2,
                 expected_balls: Optional[int] = None,
                 window: int = 48,
                 device: Optional[str] = None,
                 compile: bool = False):
        self.prompt = prompt
        self.checkpoint_path = checkpoint_path
        self.version = version
        self.score_threshold = score_threshold
        self.expected_balls = expected_balls
        self.window = window
        self.device = device
        self.compile = compile

        # Live buffer state.
        self._buffer: list = []           # PIL Images (RGB)
        self._last_tracks: List[Track] = []
        self._handoff: Optional[Dict[int, list]] = None  # obj_id -> xyxy box
        self._next_track_id = 1
        self._obj_to_track: Dict[int, int] = {}
        self._predictor = None
        self._load()

    # ── model loading ──────────────────────────────────────────────────

    def _load(self) -> None:
        try:
            import torch
        except ImportError as exc:
            raise RuntimeError("PyTorch is required for the SAM 3 tracker.") from exc

        cuda = torch.cuda.is_available()
        if not cuda:
            LOG.warning(
                "No CUDA device found — the official SAM 3 code targets CUDA "
                "(needs CUDA 12.6+, torch>=2.7 on the GPU box). CPU execution "
                "will be extremely slow or unavailable; continuing anyway."
            )
        if not os.path.isfile(self.checkpoint_path):
            raise RuntimeError(
                f"SAM 3 checkpoint not found at {self.checkpoint_path}. "
                "Get it without any HF license form from the un-gated "
                "mirror: wget -O models/sam3/sam3.pt "
                "https://huggingface.co/1038lab/sam3/resolve/main/sam3.pt"
            )

        try:
            from sam3.model_builder import build_sam3_predictor
        except ImportError as exc:
            raise RuntimeError(
                "sam3 package not installed. Clone & install the official "
                "repo: git clone https://github.com/facebookresearch/sam3 "
                ".third_party/sam3 && pip install -e .third_party/sam3 "
                "(plus einops iopath hydra-core triton pycocotools psutil "
                "timm pandas submitit)."
            ) from exc

        dev = self.device or ("cuda" if cuda else None)
        try:
            if dev in (None, "cuda") and cuda:
                self._predictor = build_sam3_predictor(
                    checkpoint_path=self.checkpoint_path,
                    version=self.version, compile=self.compile)
            else:
                # Non-CUDA devices: "cpu" for offline validation on any
                # machine, "xpu" for Intel Arc iGPU experiments. The
                # wrapper hardcodes .cuda(), so build the video model
                # ourselves and reuse the base session/handle_request API.
                dev = dev or "cpu"
                if dev == "cuda":
                    raise RuntimeError(
                        "CUDA requested but torch.cuda.is_available() is "
                        "False. Live SAM 3 inference needs the GPU box; "
                        "use --sam-device cpu for slow offline validation."
                    )
                from . import _torch_shim
                _torch_shim.enable(dev)  # redirect hardcoded cuda→dev
                from sam3.model_builder import build_sam3_video_model
                from sam3.model.sam3_base_predictor import Sam3BasePredictor
                from sam3.model.sam3_video_predictor import Sam3VideoPredictor
                predictor = Sam3VideoPredictor.__new__(Sam3VideoPredictor)
                Sam3BasePredictor.__init__(predictor)
                predictor.async_loading_frames = False
                predictor.video_loader_type = "cv2"
                predictor.model = build_sam3_video_model(
                    checkpoint_path=self.checkpoint_path,
                    load_from_HF=False,
                    device=dev,
                ).eval()
                self._predictor = predictor
        except RuntimeError:
            raise
        except Exception as exc:
            raise RuntimeError(
                f"Failed to initialise SAM 3 predictor on {dev}: {exc}"
            ) from exc
        LOG.info("SAM 3 loaded: ckpt=%s version=%s",
                 self.checkpoint_path, self.version)

    # ── public tracker interface ───────────────────────────────────────

    def start_session(self, sample_frame: np.ndarray) -> None:  # noqa: ARG002
        self._buffer.clear()
        self._last_tracks = []
        self._handoff = None
        self._obj_to_track = {}
        self._next_track_id = 1

    def track(self, frame_bgr: np.ndarray) -> List[Track]:
        """Buffer one live frame; run a window session when full.

        Returns the most recent processed frame's tracks (positions hold
        between inference windows so the display overlay interpolates).
        """
        from PIL import Image

        self._buffer.append(Image.fromarray(frame_bgr[:, :, ::-1]))  # RGB
        if len(self._buffer) >= self.window:
            self._process_window()
            self._buffer = self._buffer[-self.window // 4:]  # small overlap
        return list(self._last_tracks)

    # ── sliding-window session runner ──────────────────────────────────

    def _process_window(self) -> None:
        frames = list(self._buffer)
        resp = self._predictor.handle_request(request=dict(
            type="start_session", resource_path=frames,
            offload_video_to_cpu=True, offload_state_to_cpu=True,
        ))
        session_id = resp["session_id"]
        try:
            # ID handoff from previous window: re-feed boxes as prompts.
            prompt_req = dict(type="add_prompt", session_id=session_id,
                              frame_index=0, text=self.prompt)
            if self._handoff:
                # xyxy -> xywh (normalized; predictor uses rel coordinates).
                h_px, w_px = frames[0].size[1], frames[0].size[0]
                boxes_xywh, box_obj_ids = [], []
                for obj_id, (x1, y1, x2, y2) in self._handoff.items():
                    boxes_xywh.append([
                        x1 / w_px, y1 / h_px,
                        (x2 - x1) / w_px, (y2 - y1) / h_px,
                    ])
                    box_obj_ids.append(obj_id)
                prompt_req["bounding_boxes"] = boxes_xywh
                prompt_req["obj_id"] = box_obj_ids
            self._predictor.handle_request(request=prompt_req)

            per_frame = {}
            for out in self._predictor.handle_stream_request(request=dict(
                    type="propagate_in_video", session_id=session_id)):
                per_frame[out["frame_index"]] = out["outputs"]

            self._obj_to_track.pop(None, None)
            last_fi = max(per_frame) if per_frame else None
            for fi in sorted(per_frame):
                outputs = per_frame[fi]
                tracks = self._outputs_to_tracks(outputs)
                if fi == last_fi:
                    self._last_tracks = tracks
            # Build handoff boxes from the final tracked frame.
            self._handoff = {}
            for t in self._last_tracks:
                for obj_id, tid in self._obj_to_track.items():
                    if tid == t.track_id:
                        self._handoff[obj_id] = list(t.box)
        finally:
            self._predictor.handle_request(request=dict(
                type="close_session", session_id=session_id))

    def _outputs_to_tracks(self, outputs: dict) -> List[Track]:
        obj_ids = outputs.get("out_obj_ids",
                              outputs.get("object_ids", np.zeros(0)))
        boxes = outputs.get("out_boxes_xywh",
                            outputs.get("boxes", np.zeros((0, 4))))
        masks = outputs.get("out_binary_masks",
                            outputs.get("masks", None))
        scores = (outputs.get("out_probs", outputs.get("out_scores",
                                                      outputs.get("scores", None)))
                  if isinstance(outputs, dict) else None)

        tracks: List[Track] = []
        masks_np = masks if isinstance(masks, np.ndarray) else None
        for i, oid in enumerate(obj_ids):
            oid = int(oid)
            x, y, w, h = (float(v) for v in boxes[i]) if i < len(boxes) \
                else (0, 0, 0, 0)
            box = (x, y, x + w, y + h)
            score = float(scores[i]) if scores is not None and i < len(scores) \
                else 1.0
            if score < self.score_threshold:
                continue
            centroid, mask_area = ((x + w / 2, y + h / 2), 0)
            if masks_np is not None and i < len(masks_np):
                m = masks_np[i].astype(bool)
                mask_area = int(m.sum())
                if mask_area > 0:
                    ys, xs = np.nonzero(m)
                    centroid = (float(xs.mean()), float(ys.mean()))
            tid = self._obj_to_track.setdefault(oid, self._next_track_id)
            if tid == self._next_track_id:
                self._next_track_id += 1
            tracks.append(Track(track_id=tid, box=box, score=score,
                                centroid=centroid, mask_area=mask_area))

        if self.expected_balls:
            tracks = sorted(tracks, key=lambda t: t.score, reverse=True)
            tracks = tracks[: self.expected_balls]
        return tracks


class LegacyColorTracker:
    """HSV color-blob tracker (baseline for A/B comparison).

    Detects saturated bright blobs via HSV thresholding + morphology and
    associates them frame-to-frame by nearest centroid (greedy). Kept
    deliberately simple — it is the same family of approach as the old
    phone tracker, so it inherits its known weaknesses.
    """

    name = "legacy"

    def __init__(self, hue_center: int = 10, hue_tol: int = 20,
                 sat_min: int = 120, val_min: int = 120,
                 expected_balls: Optional[int] = None,
                 max_jump_px: float = 150.0, lost_frames: int = 15):
        import cv2
        self.cv2 = cv2
        self.hue_lo = (hue_center - hue_tol) % 180
        self.hue_hi = (hue_center + hue_tol) % 180
        self.sat_min, self.val_min = sat_min, val_min
        self.expected_balls = expected_balls
        self.max_jump_px = max_jump_px
        self.lost_frames = lost_frames
        self._next_id = 1
        self._active = {}  # id -> dict(cx, cy, missed)
        self._last_ts = None

    def start_session(self, sample_frame: np.ndarray) -> None:  # noqa: ARG002
        self._active.clear()
        self._next_id = 1

    def _hsv_mask(self, hsv: np.ndarray) -> np.ndarray:
        cv2 = self.cv2
        lo = np.array([self.hue_lo, self.sat_min, self.val_min], np.uint8)
        hi = np.array([self.hue_hi, 255, 255], np.uint8)
        if self.hue_lo <= self.hue_hi:
            mask = cv2.inRange(hsv, lo, hi)
        else:  # hue wrap-around (e.g. red)
            mask = cv2.inRange(hsv, lo, np.array([179, 255, 255], np.uint8))
            mask |= cv2.inRange(hsv, np.array([0, self.sat_min, self.val_min],
                                              np.uint8), hi)
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN,
                                cv2.getStructuringElement(cv2.MORPH_ELLIPSE,
                                                          (5, 5)))
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE,
                                cv2.getStructuringElement(cv2.MORPH_ELLIPSE,
                                                          (9, 9)))
        return mask

    def track(self, frame_bgr: np.ndarray) -> List[Track]:
        cv2 = self.cv2
        now = time.time()
        dt = (now - self._last_ts) if self._last_ts else 0.0
        self._last_ts = now

        hsv = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2HSV)
        mask = self._hsv_mask(hsv)
        n, _labels, stats, centroids = cv2.connectedComponentsWithStats(
            mask, connectivity=8
        )
        detections = []
        for i in range(1, n):  # skip background
            area = int(stats[i, cv2.CC_STAT_AREA])
            if area < 40:
                continue
            cx, cy = float(centroids[i][0]), float(centroids[i][1])
            w = float(stats[i, cv2.CC_STAT_WIDTH])
            h = float(stats[i, cv2.CC_STAT_HEIGHT])
            detections.append((cx, cy, w, h, area))
        if self.expected_balls:
            detections.sort(key=lambda d: d[4], reverse=True)
            detections = detections[: self.expected_balls]

        # Greedy nearest-neighbor association.
        assigned = set()
        new_active = {}
        for tid, st in self._active.items():
            best, best_d = None, self.max_jump_px * (1.0 + dt)  # scale by dt
            for j, (cx, cy, *_rest) in enumerate(detections):
                if j in assigned:
                    continue
                d = float(np.hypot(cx - st["cx"], cy - st["cy"]))
                if d < best_d:
                    best, best_d = j, d
            if best is not None:
                assigned.add(best)
                st.update(cx=detections[best][0], cy=detections[best][1],
                          missed=0, det=detections[best])
                new_active[tid] = st
            elif st["missed"] < self.lost_frames:
                st["missed"] += 1
                new_active[tid] = st

        tracks: List[Track] = []
        for j, (cx, cy, w, h, area) in enumerate(detections):
            if j in assigned:
                continue
            tid = self._next_id
            self._next_id += 1
            new_active[tid] = {"cx": cx, "cy": cy, "missed": 0,
                               "det": detections[j]}

        # Emit only tracks with a live detection this frame.
        for tid, st in new_active.items():
            if "det" not in st:
                continue
            cx, cy, w, h, area = st["det"]
            st.pop("det")
            tracks.append(Track(
                track_id=tid,
                box=(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2),
                score=min(1.0, area / 2000.0),
                centroid=(cx, cy),
                mask_area=area,
            ))
        self._active = new_active
        return tracks


def make_tracker(kind: str, **kwargs):
    if kind == "sam3":
        return Sam3Tracker(**kwargs)
    if kind == "legacy":
        return LegacyColorTracker(**kwargs)
    raise ValueError(f"Unknown tracker kind: {kind!r}")
