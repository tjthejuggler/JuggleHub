"""RealSense RGB exposure control + repeated-colour-frame guard.

WHY THIS FILE EXISTS (2026-11-12 audit)
---------------------------------------
MEASURED (offline, on the saved clips and recordings):
  The old code passed 2000 / 4000 / 8000 to the RGB sensor's
  `rs.option.exposure`, labelled "2 ms / 4 ms / 8 ms". At those settings the
  colour stream repeated each image many times while depth stayed at 30 unique
  fps, i.e. `wait_for_frames()` re-delivered a stale colour frame:

    value passed   labelled   repeat run   unique colour fps
    auto           -          1            30.0
    2000           2 ms       6            5.1
    4000           4 ms       12           2.6
    8000           8 ms       24           1.3

  Both 2026-11-11 LED recordings show the same 6x repeat (about 300 unique
  colour frames out of about 1800).

INFERRED, NOT YET CONFIRMED ON THE CAMERA:
  * Unit. The colour frame period scales linearly with the value and equals
    value x 100 us (2000 -> 200 ms -> 5 fps, and so on), so the RGB exposure
    option is most likely in units of 100 us rather than 1 us. Outside
    evidence is mixed and none of it is official documentation: a user report
    in librealsense issue #8243 says the RGB exposure "appears to be set in
    hundreds of microseconds", while a search snippet for issue #12006 says
    the RGB unit is microseconds. It has not been checked on the device (no
    camera was attached). `RGB_EXPOSURE_UNIT_MS` encodes this assumption; see
    "CONFIRM WITH THE CAMERA" below.
  * Stickiness. The 15-49-52 session was meant to be auto exposure but has
    the 2000-setting signature. One explanation is that a manual exposure
    persists across pipeline restarts until auto exposure is switched back
    on; another is that the lock env var was simply set for that run. Which
    one happened is unknown.

What this module does about it — both hold whichever explanation is right:
  1. Every script states exposure in MILLISECONDS and goes through
     `configure_rgb`, which converts units, refuses exposures longer than the
     frame period, ALWAYS writes the auto-exposure flag explicitly, and reads
     the resulting state back into meta.json.
  2. Every capture loop feeds `FrameGuard`, which counts colour frames that
     repeat the previous one, so a broken stream is visible on screen while
     recording and is written into meta.json. This check does not depend on
     the unit assumption.

CONFIRM WITH THE CAMERA (first run with a D455 attached):
  `configure_rgb` prints and stores `exposure_range_units`. Then set
  TRACKING_EXPOSURE_MS=20 and watch the live view: if the unit assumption is
  right the image is normally exposed and FrameGuard reports 0% repeats; if
  the unit is really 1 us the image will be almost black (200 us exposure).
  If the assumption is wrong, the failure is a dark image, not dropped frames.

UNTESTED: no camera was attached when this was written. `configure_rgb` has
never been executed against a device. `FrameGuard` / `scan_recording` are
exercised offline against the existing recordings only.
"""
from __future__ import annotations

import hashlib
import os
import sys
from collections import deque

# ASSUMPTION (inferred from repeat-frame rates, not confirmed on the device or
# in the librealsense docs): one RGB exposure unit = 100 us. See module docstring.
RGB_EXPOSURE_UNIT_MS = 0.1
DUP_INVALID_PCT = 5.0           # a recording with more repeats than this is unusable


def exposure_request_from_env():
    """-> (exposure_ms | None, gain | None). None = auto exposure.

    TRACKING_EXPOSURE_MS=<float>  manual exposure in real milliseconds
    TRACKING_GAIN=<float>         manual gain (only used with a manual exposure)
    """
    for old in ("TRACKING_EXPOSURE_LOCK", "TRACKING_LED_LOCK"):
        if os.environ.get(old):
            print(f"WARNING: {old} is retired and ignored. Its 2000 preset "
                  f"produced 5 unique colour fps on saved recordings (measured); "
                  f"the presets were probably 100x longer than labelled (unit "
                  f"not yet confirmed). Use TRACKING_EXPOSURE_MS=<ms> "
                  f"[TRACKING_GAIN=<g>] instead.",
                  file=sys.stderr)
    ms = os.environ.get("TRACKING_EXPOSURE_MS")
    gain = os.environ.get("TRACKING_GAIN")
    return (float(ms) if ms else None, float(gain) if gain else None)


def configure_rgb(profile, exposure_ms: float | None = None,
                  gain: float | None = None, fps: int = 30) -> dict:
    """Put the RGB sensor in a KNOWN exposure state and report what it is.

    exposure_ms=None -> auto exposure ON (explicitly; undoes a sticky manual
    setting) with auto_exposure_priority OFF so the camera may not trade frame
    rate for exposure in a dim room.
    Returns a dict suitable for meta.json. Failures are printed, never hidden.
    """
    import pyrealsense2 as rs

    state: dict = dict(requested_exposure_ms=exposure_ms, requested_gain=gain,
                       exposure_unit_ms=RGB_EXPOSURE_UNIT_MS, errors=[])
    sensor = None
    for s in profile.get_device().query_sensors():
        if s.get_info(rs.camera_info.name) == "RGB Camera":
            sensor = s
            break
    if sensor is None:
        state["errors"].append("no 'RGB Camera' sensor on this device")
        print("WARNING: no RGB Camera sensor found; exposure left untouched",
              file=sys.stderr)
        return state

    def _set(opt, value) -> None:
        try:
            sensor.set_option(opt, value)
        except Exception as e:  # report, don't swallow
            state["errors"].append(f"set {opt}={value}: {e}")
            print(f"WARNING: could not set {opt}={value}: {e}", file=sys.stderr)

    if exposure_ms is None:
        _set(rs.option.enable_auto_exposure, 1)
        if sensor.supports(rs.option.auto_exposure_priority):
            _set(rs.option.auto_exposure_priority, 0)   # constant frame rate
    else:
        period_ms = 1000.0 / fps
        if exposure_ms > period_ms:
            raise ValueError(
                f"exposure {exposure_ms} ms is longer than the {period_ms:.1f} ms "
                f"frame period at {fps} fps — the camera would drop frames")
        units = max(1, int(round(exposure_ms / RGB_EXPOSURE_UNIT_MS)))
        _set(rs.option.enable_auto_exposure, 0)
        _set(rs.option.exposure, units)
        if gain is not None:
            _set(rs.option.gain, gain)

    for name, opt in (("auto_exposure", rs.option.enable_auto_exposure),
                      ("exposure_units", rs.option.exposure),
                      ("gain", rs.option.gain)):
        try:
            state[name] = float(sensor.get_option(opt))
        except Exception as e:
            state["errors"].append(f"get {name}: {e}")
    if "exposure_units" in state:
        # meaningful only when auto_exposure == 0 (it is the manual register)
        state["manual_exposure_ms"] = round(
            state["exposure_units"] * RGB_EXPOSURE_UNIT_MS, 3)
    try:
        r = sensor.get_option_range(rs.option.exposure)
        state["exposure_range_units"] = [r.min, r.max, r.step, r.default]
    except Exception:
        pass
    mode = ("AUTO" if state.get("auto_exposure") == 1.0 else
            f"MANUAL {state.get('manual_exposure_ms')} ms gain {state.get('gain')}")
    print(f"RGB exposure: {mode}" + (f"  errors={state['errors']}"
                                     if state["errors"] else ""))
    # [min, max, step, default] in the sensor's own units; needed to confirm
    # the unit assumption (see "CONFIRM WITH THE CAMERA" in the module docstring)
    print(f"RGB exposure_range_units: {state.get('exposure_range_units', 'unreadable')}"
          f"  (assumed {RGB_EXPOSURE_UNIT_MS} ms per unit, unconfirmed)")
    return state


def _signature(image) -> bytes:
    return hashlib.md5(image[::8, ::8].tobytes()).digest()


class FrameGuard:
    """Counts colour frames whose content repeats the previous one.

    Compares image content (subsampled hash), which is the signal measured on
    the broken recordings: a re-delivered frame is pixel-identical, while two
    genuinely new frames always differ by sensor noise.
    """

    def __init__(self, window: int = 90):
        self.total = 0
        self.unique = 0
        self._last = None
        self._recent: deque = deque(maxlen=window)

    def update(self, image) -> bool:
        """-> True when this frame is new."""
        key = _signature(image)
        new = key != self._last
        self._last = key
        self.total += 1
        self.unique += int(new)
        self._recent.append(new)
        return new

    def reset(self) -> None:
        self.__init__(self._recent.maxlen)

    @property
    def dup_pct(self) -> float:
        return 100.0 * (self.total - self.unique) / max(self.total, 1)

    @property
    def recent_dup_pct(self) -> float:
        n = len(self._recent)
        return 100.0 * (n - sum(self._recent)) / max(n, 1)

    def warning(self, fps: float = 30.0) -> str | None:
        """Short on-screen warning when the live stream is repeating frames."""
        if len(self._recent) < 30 or self.recent_dup_pct <= DUP_INVALID_PCT:
            return None
        true_fps = fps * (1.0 - self.recent_dup_pct / 100.0)
        return (f"BAD STREAM: {self.recent_dup_pct:.0f}% repeated colour frames "
                f"(true ~{true_fps:.0f} fps)")

    def meta(self) -> dict:
        return dict(color_frames_total=self.total, color_frames_unique=self.unique,
                    color_duplicate_pct=round(self.dup_pct, 1))


def scan_recording(rgb_files: list) -> dict:
    """Offline integrity check of a saved recording: byte-identical consecutive
    colour frames are repeats (the JPEG encoder is deterministic)."""
    total = len(rgb_files)
    unique, prev, runs, run = 0, None, [], 0
    for f in rgb_files:
        with open(f, "rb") as fh:
            h = hashlib.md5(fh.read()).digest()
        if h != prev:
            if run:
                runs.append(run)
            unique += 1
            run = 1
        else:
            run += 1
        prev = h
    if run:
        runs.append(run)
    runs.sort()
    dup_pct = 100.0 * (total - unique) / max(total, 1)
    return dict(frames=total, unique=unique, duplicate_pct=round(dup_pct, 1),
                median_run=runs[len(runs) // 2] if runs else 0,
                valid=dup_pct <= DUP_INVALID_PCT)
