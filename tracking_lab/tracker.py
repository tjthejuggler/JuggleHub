"""Multi-ball juggling tracker: motion-first identity, physics-aware.

Design decisions (differs from engine New3DTracker):
- Identity comes from MOTION (Kalman prediction + Hungarian assignment),
  not from color. Color is only a soft appearance cost — so same-color
  balls remain distinct while airborne.
- HELD / IN_FLIGHT / COAST states per track; COAST rides the Kalman
  prediction through occlusion instead of teleporting to detections.
- Throw/catch events require a minimum dwell (frames) to fire, killing
  the THROW/CATCH alternation spam seen in engine recording logs.
"""
from __future__ import annotations

import itertools
from dataclasses import dataclass, field

import numpy as np

try:  # scipy is optional; fall back to greedy
    from scipy.optimize import linear_sum_assignment
    HAVE_SCIPY = True
except ImportError:
    HAVE_SCIPY = False


@dataclass
class TrackingConfig:
    fps: float = 30.0
    n_balls: int = 3                    # expected ball count (top-N gating)
    assoc_gate_px: float = 80.0         # max distance for candidate association
    appearance_weight: float = 0.3      # color-hist cost weight (0 = motion only)
    coast_frames: int = 30              # frames a lost track coasts before death
    gravity_px_s2: float = 2500.0       # image-space gravity prior (tuned for 640x480)
    new_track_score: float = 0.5        # min detector score to spawn a track
    held_radius_px: float = 75.0        # ball-to-wrist distance => held
    held_dwell: int = 2                 # frames inside radius before CATCH fires
    min_catch_gap: int = 4              # same-hand catches closer than this dedup
    throw_dwell: int = 3                # frames outside radius+velocity before THROW
    min_flight_frames: int = 2          # min away-frames at first wrist contact
    max_catch_speed: float = 450.0      # px/s: catch/transfer needs decelerated ball
    held_max_occluded: int = 10         # HELD track vanishes >this long => THROW
    face_radius_px: float = 55.0        # face false-det suppression radius
    face_stall_frames: int = 20         # track stationary on face this long => kill
    min_hits: int = 3                   # consecutive hits before a new track activates
    depth_gate_m: float = 0.6           # max |Δz| for association when depth exists
    # T5 Defect-1 fix, default OFF so the §3 baseline stays identical:
    # when a track becomes HELD in hand H, any OTHER track still HELD in H
    # with no detection this frame (misses > 0) is stale — a ghost left by
    # double-counting — and blocks the hand from re-catching. release_stale_
    # held releases it silently (variant a); release_stale_held_throw also
    # emits a THROW dated at this frame (variant b — a guess that may also
    # close the throw under-count). b requires a.
    release_stale_held: bool = False
    release_stale_held_throw: bool = False
    # variant (c): kill the stale ghost outright instead of releasing it
    # (release measured WORSE 2026-11-12: the released ghost sits at the
    # wrist as IN_FLIGHT and steals the incoming ball's detection, turning
    # misses up — box30 per-event missed 11 -> 16, held-out 6.33 -> 7.17)
    release_stale_held_kill: bool = False
    # variant (d): skip the straight-across transfer branch entirely.
    # Measured with ground truth 2026-11-12: transfer CATCHes are a coin
    # flip (box30 5/10 real, cascade60 3/6) while flight-backed are 94-96%
    # real; phantoms corrupt identity and park balls at wrists un-catchable.
    disable_transfer: bool = False
    # T6 option 1 (read by detcache.replay, not the tracker): run the pose
    # network every n-th frame and hold the last wrists in between. 1 = the
    # scored default. Changes detections -> counts; judge on per-event
    # marks + held-out, never on speed alone.
    pose_every_n: int = 1
    # Fix B (T5, per-event evidence 2026-11-12): a ball parked at a wrist
    # for this many frames is counted as caught even with no recorded
    # flight history (away_peak==0 trap: transferred/carried balls can
    # dwell at a wrist forever without ever legally "being away" — box30
    # f29 measured 13 frames glued, no event). 0 = off (the scored
    # default). See also f200/f214 for Fix C.
    dwell_catch_frames: int = 0
    # Fix C (T5, per-event evidence 2026-11-12): the straight-across
    # transfer branch must not emit a CATCH at a hand that VISIBLY holds
    # another detected HELD ball (two HELD tracks at one hand+position
    # measured at box30 f200/f214; transfer CATCHes measured a coin flip
    # vs ground truth: 5/10, 3/6). Blocks the emit only; veto-follow
    # behavior unchanged. Do NOT disable the branch (variant d proved it
    # is box's only observer). False = off (the scored default).
    transfer_twin_guard: bool = False
    # one canonical HELD identity per hand: the same-hand catch dedup must not
    # mint a second HELD track, a legal re-catch displaces a stale occupant,
    # and a transfer retires a ghost occupant before latching (T5 redesign).
    canonical_held: bool = False
    # sub-switches of canonical_held (each measured independently):
    canonical_dedup_merge: bool = False   # deduped catch must not latch HELD
    canonical_transfer_ghost: bool = False  # transfer retires ghost occupant
    canonical_displace: bool = False     # legal re-catch displaces occupant
    canonical_dwell_vacant: bool = False  # dwell-promote only into vacant hand
    # variant (e): the f200/f214 box30 deadlock — two tracks HELD at the
    # SAME hand at the SAME position (misses=0, glued by held-follow),
    # silently absorbing every incoming ball. On a fresh catch in hand H,
    # kill any OTHER track HELD in H whose position is within this radius
    # (same physical ball, duplicated identity). (d) measured that the
    # transfer branch is load-bearing for box; (a)-(c) missed because the
    # ghosts have misses=0. 0 disables.
    kill_twin_held_radius_px: float = 0.0


@dataclass
class BallTrack:
    track_id: int
    state: str = "IN_FLIGHT"            # IN_FLIGHT | HELD | COAST
    x: float = 0.0
    y: float = 0.0
    vx: float = 0.0
    vy: float = 0.0
    z: float | None = None              # depth (m) if available
    age: int = 0
    hits: int = 0
    misses: int = 0
    score: float = 0.0
    hist: np.ndarray | None = None      # color histogram (appearance)
    held_hand: str | None = None
    held_frames: int = 0
    away_frames: int = 0
    history: list = field(default_factory=list)

    def predict(self, dt: float, g: float):
        self.x += self.vx * dt
        self.y += self.vy * dt
        self.vy += g * dt
        self.age += 1


def _color_hist(rgb, box, bins=16):
    x1, y1, x2, y2 = [int(v) for v in box]
    roi = rgb[max(0, y1):y2, max(0, x1):x2]
    if roi.size == 0:
        return None
    hsv_roi = roi  # already BGR from cv2; use hue channel
    h = cv2_hsv_hue(hsv_roi)
    if h is None:
        return None
    hh, _ = np.histogram(h, bins=bins, range=(0, 180))
    s = hh.sum() or 1
    return hh.astype(np.float32) / s


def cv2_hsv_hue(bgr_roi):
    import cv2
    hsv = cv2.cvtColor(bgr_roi, cv2.COLOR_BGR2HSV)
    return hsv[..., 0].reshape(-1)


class JuggleTracker:
    """Per-frame: step(frame_idx, detections, wrists, depth_fn) -> events."""

    def __init__(self, cfg: TrackingConfig | None = None):
        self.cfg = cfg or TrackingConfig()
        self.tracks: list[BallTrack] = []
        self._next_id = 1
        self.events: list[dict] = []
        self.per_frame: list[dict] = []
        # static-clutter filter: bins where a detection appears almost every
        # frame (lights, furniture highlights) are suppressed
        self._bin_hits: dict = {}
        self._frames_seen = 0
        # recently-dead established tracks for ID stitching
        self._dead: list[dict] = []
        # recent silent HELD releases: if the ball surfaces at the OTHER hand
        # quickly, it was a carry -> emit CATCH (improvement #3)
        self._carries: list[dict] = []
        # last CATCH frame per hand: debounces same-hand double reports while
        # still allowing a DIFFERENT track to be caught at the same hand
        self._last_catch_frame: dict = {}
        self._last_catch_track: dict = {}
        # diagnostics only (never read by the tracking logic): every time a
        # HELD track is released WITHOUT a THROW event, and every CATCH the
        # same-hand dedup swallowed. tracking_lab.evaluate reports these so
        # hidden error-cancellation is visible instead of buried in a total.
        self.silent_releases: list[dict] = []
        self.deduped_catches: list[dict] = []
        # diagnostics only (never read by the tracking logic): every stale
        # HELD ghost released by the release_stale_held flag
        self.stale_releases: list[dict] = []
        # diagnostics only: every time canonical_held refuses to mint a second
        # HELD identity (merge) or displaces a stale occupant
        self.merged_held: list[dict] = []

    def _release_stale_held(self, hand: str, frame_idx: int, fired: list,
                            cfg: "TrackingConfig") -> None:
        """Defect-1 (T5): called right after a track became HELD in `hand`.
        Releases any OTHER track still HELD in the same hand that had no
        detection this frame — it is a stale ghost, and while it sits HELD
        the hand cannot re-catch (measured on box30: 11 missed catches,
        2held% 35.2; on 441_30 a track stayed HELD 63 frames at the wrist
        mid-pattern)."""
        if not getattr(cfg, "release_stale_held", False) and \
                not getattr(cfg, "kill_twin_held_radius_px", 0.0):
            return
        for other in self.tracks:
            if (other.state == "HELD" and other.held_hand == hand
                    and other.track_id != getattr(self, "_last_caught_id", -1)
                    and getattr(cfg, "kill_twin_held_radius_px", 0.0) > 0
                    and float(np.hypot(other.x - self._last_caught_x,
                                       other.y - self._last_caught_y))
                    < cfg.kill_twin_held_radius_px):
                self.stale_releases.append(dict(frame=frame_idx,
                                                track=other.track_id,
                                                hand=hand, twin=True))
                other.misses = self.cfg.coast_frames + 1  # die this step
                continue
            if (other.state == "HELD" and other.held_hand == hand
                    and other.misses > 0):
                self.stale_releases.append(dict(frame=frame_idx,
                                                track=other.track_id,
                                                hand=hand))
                if getattr(cfg, "release_stale_held_kill", False):
                    # variant (c): remove it from play entirely so it cannot
                    # compete for detections (misses beyond coast_frames dies
                    # at the end of this step, same as the face-stall kill)
                    other.misses = self.cfg.coast_frames + 1
                    continue
                if getattr(cfg, "release_stale_held_throw", False):
                    fired.append(dict(frame=frame_idx, event="THROW",
                                      track=other.track_id, hand=hand,
                                      vx=round(other.vx), vy=round(other.vy)))
                other.state = "IN_FLIGHT"
                other.held_hand = None
                other.held_frames = 0
                other.flight_frames = 0
                other.away_peak = 0
                other.away_from_caught = 0

    @staticmethod
    def _retire_held(track: "BallTrack") -> None:
        """Silently release a HELD track back to IN_FLIGHT (no event)."""
        track.state = "IN_FLIGHT"
        track.held_hand = None
        track.held_frames = 0
        track.flight_frames = 0
        track.away_peak = 0
        track.away_from_caught = 0

    def _suppress_static(self, dets: list) -> list:
        self._frames_seen += 1
        bin_sz = 32
        out = []
        for d in dets:
            box = d[0]
            key = (int((box[0] + box[2]) / 2) // bin_sz,
                   int((box[1] + box[3]) / 2) // bin_sz)
            self._bin_hits[key] = self._bin_hits.get(key, 0) + 1
            # require >85% occupancy over at least 30 frames -> never-moving FP
            if (self._frames_seen > 30
                    and self._bin_hits[key] > 0.85 * self._frames_seen):
                continue
            out.append(d)
        return out

    # ── association helpers ─────────────────────────────────────────
    def _cost(self, t: BallTrack, det, wrist_dist: float | None) -> float | None:
        box, score, _cls, _hist = det
        dx = (box[0] + box[2]) / 2 - t.x
        dy = (box[1] + box[3]) / 2 - t.y
        dist = np.hypot(dx, dy)
        if dist > self.cfg.assoc_gate_px * (1 + 0.5 * min(t.misses, 4)):
            return None
        cost = dist
        if self.cfg.appearance_weight > 0 and t.hist is not None:
            h = det[3] if len(det) > 3 else None
            if h is not None:
                inter = np.minimum(t.hist, h).sum()
                hist_cost = 1.0 - inter  # 0 identical .. 1 disjoint
                cost += self.cfg.appearance_weight * 60.0 * hist_cost
        return cost

    def step(self, frame_idx: int, detections: list, wrists: dict, rgb=None,
             depth_at=None, face_pt=None) -> list[dict]:
        """detections: [(box, score, cls, hist_or_None)]; wrists: {hand:(x,y)};
        face_pt: (x,y) of the juggler's face (suppresses face-phantom tracks)."""
        cfg = self.cfg
        detections = self._suppress_static(detections)
        dt = 1.0 / cfg.fps
        fired = []

        # 1. predict all tracks forward
        for t in self.tracks:
            t.predict(dt, cfg.gravity_px_s2 if t.state != "HELD" else 0.0)

        # 2. build cost matrix track x detection
        dets = []
        for d in detections:
            box, score, cls = d[0], d[1], d[2]
            hist = d[3] if len(d) > 3 else None
            dets.append((box, score, cls, hist))
        # soft top-N: keep expected count + headroom so a real ball never gets
        # sliced out while a phantom outranks it (association + pending gate
        # filter the extras)
        if len(self.tracks) >= cfg.n_balls:
            dets = sorted(dets, key=lambda d: -d[1])[: cfg.n_balls + 2]

        n_t, n_d = len(self.tracks), len(dets)
        if n_t and n_d:
            cost = np.full((n_t, n_d), 1e6)
            for i, t in enumerate(self.tracks):
                for j, d in enumerate(dets):
                    c = self._cost(t, d, None)
                    if c is not None:
                        cost[i, j] = c
            if HAVE_SCIPY:
                rows, cols = linear_sum_assignment(cost)
                pairs = [(r, c) for r, c in zip(rows, cols)
                         if cost[r, c] < 1e5]
            else:
                pairs = _greedy(cost)
        else:
            pairs = []

        matched_t = {i for i, _ in pairs}
        matched_d = {j for _, j in pairs}

        # 3. update matched
        for i, j in pairs:
            t = self.tracks[i]
            box, score, cls, hist = dets[j]
            cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
            alpha = 0.4
            t.vx = alpha * ((cx - t.x) / dt) + (1 - alpha) * t.vx
            t.vy = alpha * ((cy - t.y) / dt) + (1 - alpha) * t.vy
            t.x, t.y = cx, cy
            if depth_at is not None:
                z = depth_at(cx, cy)
                if z and z > 0.1:
                    t.z = z if t.z is None else 0.7 * t.z + 0.3 * z
            t.score = score
            if hist is not None:
                t.hist = hist if t.hist is None else 0.8 * t.hist + 0.2 * hist
            t.hits += 1
            t.misses = 0
            t.lx, t.ly = t.x, t.y          # last matched position (for stitch)
            if t.state == "COAST":
                t.state = "IN_FLIGHT"

        # 4. unmatched tracks -> COAST / die; a HELD ball occluded by its hand
        #    sticks to the wrist instead of dying (fixes webcam track churn)
        for i, t in enumerate(self.tracks):
            if i in matched_t:
                continue
            near_hand, dist = _nearest_wrist(t, wrists) if wrists else (None, 1e9)
            if (t.state == "HELD" and near_hand
                    and dist < cfg.held_radius_px * 1.6):
                hx, hy = wrists[near_hand]
                t.x, t.y = 0.6 * t.x + 0.4 * hx, 0.6 * t.y + 0.4 * hy
                t.misses += 1
                if t.misses >= cfg.held_max_occluded:
                    # HELD but no matched detection for too long: the ball was
                    # thrown (or carried under the arm). SILENT release — no
                    # THROW event — so the track can catch the next arrival;
                    # if it surfaces at the OTHER hand quickly, the release
                    # handler below converts it into a carry CATCH+THROW.
                    self._carries.append(dict(track_id=t.track_id,
                                              hand=t.held_hand,
                                              frame=frame_idx))
                    self.silent_releases.append(dict(frame=frame_idx,
                                                     track=t.track_id,
                                                     hand=t.held_hand))
                    t.state = "IN_FLIGHT"
                    t.held_hand = None
                    t.held_frames = 0
                    t.flight_frames = 0
                    t.away_peak = 0
                continue
            if (t.state == "HELD"
                    and t.misses >= getattr(cfg, "held_max_occluded", 10)):
                fired.append(dict(frame=frame_idx, event="THROW",
                                  track=t.track_id, hand=t.held_hand,
                                  vx=round(t.vx), vy=round(t.vy)))
                t.state = "IN_FLIGHT"
                t.held_hand = None
                t.held_frames = 0
                t.flight_frames = 0
                t.away_peak = 0
            t.misses += 1
            if t.state == "IN_FLIGHT":
                t.state = "COAST"
            if t.misses > cfg.coast_frames:
                # remember where established tracks were last SEEN (not where
                # they coasted to) for ID stitching
                lx = getattr(t, "lx", t.x)
                ly = getattr(t, "ly", t.y)
                self._dead.append(dict(track_id=t.track_id, x=lx, y=ly,
                                       vx=t.vx, vy=t.vy, hist=t.hist,
                                       frame=frame_idx - t.misses))
        self.tracks = [t for t in self.tracks if t.misses <= cfg.coast_frames]

        # 5. unmatched detections -> new tracks (pending until min_hits)
        n_active = sum(1 for t in self.tracks if not getattr(t, "pending", False))
        for j, d in enumerate(dets):
            if j in matched_d or n_active >= cfg.n_balls:
                continue
            box, score, cls, hist = d
            if score < cfg.new_track_score:
                continue
            cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
            t = BallTrack(track_id=self._next_id, x=cx, y=cy, score=score, hist=hist)
            # ID stitching: a spawn near where a track died within ~1 s is the
            # same physical ball re-detected (crossing/blur gap), keep its ID.
            best, bd = None, 1e9
            for k, dead in enumerate(self._dead):
                age = frame_idx - dead["frame"]
                if age > int(cfg.fps):
                    continue
                # propagate the dead track's last-seen position with its velocity
                dt_d = age / cfg.fps
                px = dead["x"] + dead["vx"] * dt_d
                py = dead["y"] + dead["vy"] * dt_d + 0.5 * cfg.gravity_px_s2 * dt_d * dt_d
                ddist = np.hypot(cx - px, cy - py)
                if ddist < bd:
                    best, bd = k, ddist
            if best is not None and bd < cfg.assoc_gate_px * 1.5:
                dead = self._dead.pop(best)
                t.track_id = dead["track_id"]
                t.x, t.y = dead["x"], dead["y"]
                t.vx, t.vy = dead["vx"], dead["vy"]
                t.hist = dead["hist"] if t.hist is None else t.hist
            else:
                self._next_id += 1
            t.pending = True
            t.hits = 1
            self.tracks.append(t)

        # pending tracks die on the first miss (filters transient false positives)
        self.tracks = [t for t in self.tracks
                       if not (getattr(t, "pending", False) and t.misses > 0)]

        # 6. promote pending tracks with enough hits; prune stale carries
        for t in self.tracks:
            if getattr(t, "pending", False) and t.hits >= cfg.min_hits:
                t.pending = False
        self._carries = [c for c in self._carries
                         if frame_idx - c["frame"] <= int(cfg.fps)]

        # 7. held/flight state machine + debounced events
        #    flight_frames counts frames OUTSIDE hand radius since last HELD,
        #    so CATCH requires a genuine flight between HOLD events.
        for t in self.tracks:
            if getattr(t, "pending", False):
                continue
            near_hand, dist = _nearest_wrist(t, wrists)
            t.flight_frames = getattr(t, "flight_frames", 0)
            # face-phantom kill: a track parked on the face with near-zero
            # speed for face_stall_frames is a false detection, not a ball
            # (verified: box30 f195 track chasing a face det).
            if face_pt is not None:
                d_face = float(np.hypot(t.x - face_pt[0], t.y - face_pt[1]))
                speed = float(np.hypot(t.vx, t.vy))
                if d_face < cfg.face_radius_px and speed < 150:
                    t.face_stall = getattr(t, "face_stall", 0) + 1
                else:
                    t.face_stall = 0
                if t.face_stall >= cfg.face_stall_frames:
                    t.misses = cfg.coast_frames + 1  # kill silently
                    continue
            # per-hand away counters: was the ball away from THIS wrist?
            # (a straight-across throw never leaves both wrist radii, so a
            # global away-counter misses it; a per-hand one does not)
            away_from_caught = getattr(t, "away_from_caught", 0)
            if t.state == "HELD":
                if (near_hand == t.held_hand
                        and dist < cfg.held_radius_px):
                    # still in the catching hand: follow the hand
                    t.away_frames = 0
                    t.away_from_caught = 0
                    hx, hy = wrists[near_hand]
                    t.vx = (hx - t.x) / dt * 0.5
                    t.vy = (hy - t.y) / dt * 0.5
                    t.x, t.y = hx, hy
                elif (near_hand and near_hand != t.held_hand
                        and dist < cfg.held_radius_px
                        and t.held_hand in wrists
                        and not getattr(cfg, "disable_transfer", False)):
                    # straight-across pass / carry: the matched ball moved
                    # from the caught hand to the OTHER hand (now nearest and
                    # within radius). Emit THROW+CATCH for the transfer.
                    hx, hy = wrists[t.held_hand]
                    d_old = float(np.hypot(t.x - hx, t.y - hy))
                    speed = float(np.hypot(t.vx, t.vy))
                    # veto fast fly-bys: a real pass/carry is slow at contact;
                    # an airborne ball crossing the far wrist is fast
                    if dist < d_old - 3 and speed < cfg.max_catch_speed:
                        ghost = None
                        if getattr(cfg, "canonical_held", False):
                            ghost = next(
                                (o for o in self.tracks if o is not t
                                 and o.state == "HELD"
                                 and o.held_hand == near_hand), None)
                        twin_seen = (cfg.transfer_twin_guard
                                     or getattr(cfg, "canonical_held", False)) and any(
                            o is not t and o.state == "HELD"
                            and o.held_hand == near_hand and o.misses == 0
                            for o in self.tracks)
                        if not twin_seen:
                            if ghost is not None and \
                                    getattr(cfg, "canonical_transfer_ghost",
                                            False):
                                # canonical: retire the stale ghost occupying
                                # the target hand so it gets ONE held identity
                                self._retire_held(ghost)
                                self.merged_held.append(
                                    dict(frame=frame_idx, track=ghost.track_id,
                                         displaced_by=t.track_id,
                                         hand=near_hand, transfer=True))
                            fired.append(dict(frame=frame_idx, event="THROW",
                                              track=t.track_id, hand=t.held_hand,
                                              vx=round(t.vx), vy=round(t.vy)))
                            fired.append(dict(frame=frame_idx, event="CATCH",
                                              track=t.track_id, hand=near_hand,
                                              dist=round(dist, 1)))
                            t.state = "HELD"
                            t.held_hand = near_hand
                            t.held_since = frame_idx
                            t.held_frames = 0
                            t.flight_frames = 0
                            t.away_from_caught = 0
                            t.away_peak = 0
                            self._release_stale_held(near_hand, frame_idx,
                                                     fired, cfg)
                            continue
                        # twin guard: target hand visibly holds a ball; fall
                        # through to the veto-follow path (no events fired)
                    hx2, hy2 = wrists[near_hand]
                    t.vx = (hx2 - t.x) / dt * 0.5
                    t.vy = (hy2 - t.y) / dt * 0.5
                    t.x, t.y = hx2, hy2
                elif near_hand and dist < cfg.held_radius_px:
                    # same hand as caught, or other hand without prior leave:
                    # treat as held-follow if still the same hand
                    t.away_frames = 0
                    t.away_from_caught = 0
                    hx, hy = wrists[near_hand]
                    t.vx = (hx - t.x) / dt * 0.5
                    t.vy = (hy - t.y) / dt * 0.5
                    t.x, t.y = hx, hy
                else:
                    t.away_frames += 1
                    t.away_from_caught = away_from_caught + 1
                    if t.away_frames >= cfg.throw_dwell:
                        fired.append(dict(frame=frame_idx, event="THROW",
                                          track=t.track_id,
                                          hand=t.held_hand,
                                          vx=round(t.vx), vy=round(t.vy)))
                        # a THROW from hand H resets H's catch dedup so the
                        # next arrival can be counted as a fresh catch
                        self._last_catch_frame[t.held_hand] = -10**9
                        self._last_catch_track[t.held_hand] = None
                        t._threw_from_hand_at = frame_idx
                        t.state = "IN_FLIGHT"
                        t.held_hand = None
                        t.held_frames = 0
                        t.flight_frames = 0
                        t.away_peak = 0
            elif near_hand and dist < cfg.held_radius_px:
                # carry completion: silent-released from the other hand a
                # moment ago and surfaced here -> count CATCH (THROW implicit)
                for k, car in enumerate(self._carries):
                    if (car["hand"] != near_hand
                            and frame_idx - car["frame"] <= int(cfg.fps / 2)):
                        # carried hand-to-hand while occluded: count as CATCH
                        # (flagged) so pattern counts include transfers
                        fired.append(dict(frame=frame_idx, event="CATCH",
                                          track=t.track_id, hand=near_hand,
                                          dist=round(dist, 1), carry=True))
                        self._carries.pop(k)
                        break
                # first contact after flight: latch the away-duration NOW
                # (it resets to 0 once we sit at the wrist)
                if t.away_frames >= cfg.min_flight_frames:
                    t.away_peak = max(getattr(t, "away_peak", 0), t.away_frames)
                t.held_frames += 1
                t.away_frames = 0
                speed = float(np.hypot(t.vx, t.vy))
                # persistence check replaces the instant speed gate: a ball
                # that DWELLS at the wrist for held_dwell+5 frames was caught,
                # however fast it arrived (fast catches at 3.5/s were denied
                # by the old speed<max_catch_speed test)
                long_rest = t.held_frames >= cfg.held_dwell + 5
                # Fix B (dwell_catch_frames): a ball parked at a wrist this
                # long was caught — accept even with no recorded flight
                # history (away_peak==0 trap) and however fast it arrived.
                dwell_ok = (cfg.dwell_catch_frames > 0
                            and t.held_frames >= cfg.dwell_catch_frames)
                if getattr(cfg, "canonical_dwell_vacant", False) and \
                        not dwell_ok:
                    # strict subset of Fix B: promote a long-dwelling
                    # IN_FLIGHT track only when the hand has NO canonical
                    # HELD occupant (an occupied hand promotes phantoms)
                    occupied = any(
                        o is not t and o.state == "HELD"
                        and o.held_hand == near_hand for o in self.tracks)
                    dwell_ok = (not occupied
                                and t.held_frames >= cfg.held_dwell + 5)
                if (t.held_frames >= cfg.held_dwell
                        and (getattr(t, "away_peak", 0) >= cfg.min_flight_frames
                             or away_from_caught >= cfg.min_flight_frames
                             or dwell_ok)
                        and (speed < cfg.max_catch_speed or long_rest
                             or dwell_ok)):
                    # same-hand dedup: two catches at one hand closer than
                    # min_catch_gap frames are one physical catch UNLESS a
                    # THROW from that hand happened in between
                    last_f = self._last_catch_frame.get(near_hand, -10**9)
                    last_t = self._last_catch_track.get(near_hand)
                    threw_since = getattr(t, "_threw_from_hand_at", -10**9)
                    accept = (frame_idx - last_f >= cfg.min_catch_gap
                              or threw_since >= last_f)
                    occupant = None
                    if getattr(cfg, "canonical_held", False):
                        occupant = next(
                            (o for o in self.tracks if o is not t
                             and o.state == "HELD"
                             and o.held_hand == near_hand), None)
                    if (not accept) and occupant is not None and \
                            getattr(cfg, "canonical_dedup_merge", False):
                        # canonical: our own dedup just judged this the SAME
                        # physical catch as the occupant — do not mint a
                        # second HELD identity in this hand. The track keeps
                        # hunting instead of parking on an occupied hand.
                        t.held_frames = 0
                        t.away_frames = 0
                        self.merged_held.append(
                            dict(frame=frame_idx, track=t.track_id,
                                 merged_into=occupant.track_id,
                                 hand=near_hand))
                    else:
                        if accept and occupant is not None and \
                                getattr(cfg, "canonical_displace", False):
                            # a legal fresh catch (a THROW happened since)
                            # displaces whatever stale identity remains here
                            self._retire_held(occupant)
                            self.merged_held.append(
                                dict(frame=frame_idx, track=occupant.track_id,
                                     displaced_by=t.track_id, hand=near_hand))
                        if accept:
                            fired.append(dict(frame=frame_idx, event="CATCH",
                                              track=t.track_id, hand=near_hand,
                                              dist=round(dist, 1)))
                            self._last_catch_frame[near_hand] = frame_idx
                            self._last_catch_track[near_hand] = t.track_id
                        else:
                            self.deduped_catches.append(dict(frame=frame_idx,
                                                             track=t.track_id,
                                                             hand=near_hand))
                        t.state = "HELD"
                        t.held_hand = near_hand
                        t.held_since = frame_idx
                        t.flight_frames = 0
                        t.away_peak = 0
                        t.away_from_caught = 0
                        self._last_caught_id = t.track_id
                        self._last_caught_x, self._last_caught_y = t.x, t.y
                        self._release_stale_held(near_hand, frame_idx,
                                                 fired, cfg)
            else:
                # ball not near a wrist. tolerate short detection gaps inside
                # a wrist visit: 1-2 dropped frames must not reset the dwell
                # (fast cascades produce detection dropouts right at the catch)
                if t.held_frames > 0 and t.held_frames < cfg.held_dwell * 2:
                    t.held_frames += 1   # grace: treat gap as still-in-hand
                else:
                    t.held_frames = 0
                t.away_frames += 1
                t.flight_frames += 1
            t.history.append((frame_idx, t.x, t.y, t.state))

        self.events.extend(fired)
        active = [t for t in self.tracks if not getattr(t, "pending", False)]
        self.per_frame.append(dict(frame=frame_idx,
                                   ids=[t.track_id for t in active],
                                   states=[t.state for t in active],
                                   hands=[t.held_hand for t in active],
                                   pos=[(round(t.x, 1), round(t.y, 1)) for t in active],
                                   held_frames=[t.held_frames for t in active],
                                   wrists={k: (round(x, 1), round(y, 1))
                                           for k, (x, y) in wrists.items()}))
        return fired

    def summary(self) -> dict:
        n = len(self.per_frame)
        full = sum(1 for f in self.per_frame if len(f["ids"]) >= self.cfg.n_balls)
        id_changes = 0
        prev = None
        for f in self.per_frame:
            cur = set(f["ids"])
            if prev and len(prev & cur) == 0 and len(cur) and len(prev):
                id_changes += 1
            prev = cur
        throws = [e for e in self.events if e["event"] == "THROW"]
        catches = [e for e in self.events if e["event"] == "CATCH"]
        return dict(frames=n, full_coverage_pct=round(100 * full / max(n, 1), 1),
                    total_id_resets=id_changes, throws=len(throws),
                    catches=len(catches), tracks_created=self._next_id - 1,
                    avg_tracks=round(sum(len(f["ids"]) for f in self.per_frame) / max(n, 1), 2))


def _nearest_wrist(t: BallTrack, wrists: dict):
    best, bd = None, 1e9
    for hand, (x, y) in wrists.items():
        d = float(np.hypot(x - t.x, y - t.y))
        if d < bd:
            best, bd = hand, d
    return best, bd


def _greedy(cost: np.ndarray):
    pairs = []
    used_r, used_c = set(), set()
    flat = [(cost[i, j], i, j) for i in range(cost.shape[0])
            for j in range(cost.shape[1]) if cost[i, j] < 1e5]
    for c, i, j in sorted(flat):
        if i in used_r or j in used_c:
            continue
        used_r.add(i); used_c.add(j)
        pairs.append((i, j))
    return pairs
