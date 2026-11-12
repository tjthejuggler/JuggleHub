"""Live tracking pipeline: source -> tracker -> depth -> overlay/log."""

import logging
import time
from typing import Optional

import numpy as np

from .csv_logger import StabilityStats, TrackCsvLogger
from .depth import DepthContext, disambiguate_overlaps
from .overlay import OverlayRenderer
from .sources import SourceError, make_source
from .trackers import make_tracker
from .types import FrameResult

LOG = logging.getLogger(__name__)


class LivePipeline:
    def __init__(self, source_kind: str, tracker_kind: str,
                 prompt: str = "ball", balls: Optional[int] = None,
                 score_threshold: float = 0.5,
                 working_range_m=(1.0, 4.0),
                 csv_path: Optional[str] = None,
                 show_overlay: bool = True, headless: bool = False,
                 source_kwargs: Optional[dict] = None,
                 tracker_kwargs: Optional[dict] = None):
        self.source = make_source(source_kind, **(source_kwargs or {}))
        tkwargs = dict(tracker_kwargs or {})
        if tracker_kind == "sam3":
            tkwargs.setdefault("prompt", prompt)
            tkwargs.setdefault("score_threshold", score_threshold)
        tkwargs.setdefault("expected_balls", balls)
        self.tracker = make_tracker(tracker_kind, **tkwargs)
        self.depth_ctx = DepthContext(working_range_m=working_range_m)
        self.renderer = OverlayRenderer() if (show_overlay and not headless) else None
        self.logger = TrackCsvLogger(csv_path) if csv_path else None
        self.stats = StabilityStats(expected_balls=balls or 0)
        self.headless = headless
        self.frame_index = 0
        self._proc_fps = 0.0
        self._disp_fps = 0.0
        self._last_disp_t = time.time()

    def run(self, max_frames: Optional[int] = None) -> StabilityStats:
        import cv2

        try:
            first = self.source.read()[0]
            self.tracker.start_session(first)
            while max_frames is None or self.frame_index < max_frames:
                ok = self._step(cv2)
                if not ok:
                    break
        except KeyboardInterrupt:
            LOG.info("Interrupted by user")
        finally:
            self._shutdown(cv2)
        return self.stats

    def _step(self, cv2) -> bool:
        try:
            frame, depth, ts = self.source.read()
        except SourceError as exc:
            LOG.error("Source error: %s", exc)
            return False

        t0 = time.perf_counter()
        tracks = self.tracker.track(frame)
        inference_ms = (time.perf_counter() - t0) * 1000.0

        # Attach timestamps for trails/interp, then depth gating + 3D lift.
        for t in tracks:
            t.timestamp = ts
        if depth is not None or self.source.intrinsics is not None:
            tracks = self.depth_ctx.apply(tracks, depth,
                                          getattr(self.source,
                                                  "intrinsics", None))
        disambiguate_overlaps(tracks)

        result = FrameResult(frame_index=self.frame_index, timestamp=ts,
                             tracks=tracks, inference_ms=inference_ms,
                             source_fps=self._proc_fps)
        self.stats.update(result)
        if self.logger:
            self.logger.log_frame(result)

        self._proc_fps = self._ema(self._proc_fps, 1000.0 / max(inference_ms, 1e-6))
        now = time.time()
        self._disp_fps = self._ema(self._disp_fps, 1.0 / max(now - self._last_disp_t, 1e-6), 0.1)
        self._last_disp_t = now

        if self.renderer is not None:
            self.renderer.update_trails(tracks)
            # Interpolate when processing lags display (alpha in [0,1)).
            lag = max(0.0, min(0.9, (self._disp_fps - self._proc_fps)
                               / max(self._disp_fps, 1e-6)))
            shown = self.renderer.draw(frame.copy(), tracks,
                                       self._proc_fps, self._disp_fps,
                                       interpolation_alpha=lag)
            cv2.imshow("SAM3 Live — q to quit", shown)
            if cv2.waitKey(1) & 0xFF in (ord("q"), 27):
                return False
        elif self.frame_index % 30 == 0:
            LOG.info("frame %d: %d tracks, proc %.1f FPS (headless)",
                     self.frame_index, len(tracks), self._proc_fps)

        self.frame_index += 1
        return True

    @staticmethod
    def _ema(prev: float, sample: float, alpha: float = 0.1) -> float:
        return prev + alpha * (sample - prev)

    def _shutdown(self, cv2) -> None:
        self.source.close()
        if self.logger:
            self.logger.close()
            LOG.info("Track CSV written to %s (%d rows)",
                     self.logger.path, self.logger.rows)
        if self.renderer is not None:
            cv2.destroyAllWindows()
        LOG.info("Stability summary:\n%s", self.stats.summary())
