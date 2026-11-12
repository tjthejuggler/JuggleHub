"""tracking_lab: offline iteration harness for robust juggling tracking.

Runs the detector + tracker stack over engine recordings
(engine/data/1_raw_recordings/<session>/{no_boxes,depth}), scores track
quality, and renders review videos — so tracker changes can be iterated
without a live juggler.
"""

__version__ = "0.1.0"

# cv2's bundled Qt ships only the xcb platform plugin, but the user's desktop
# session may be Wayland (no XDG_SESSION_TYPE fallback inside Qt). Pointing Qt
# at xcb routes cv2.imshow windows through XWayland, which is present on
# virtually all Wayland desktops. Must be set before cv2 is imported, hence
# here in the package __init__ rather than in the scripts. setdefault keeps
# any explicit user override working.
import os as _os
_os.environ.setdefault("QT_QPA_PLATFORM", "xcb")

from .recording import RecordingSession
from .tracker import BallTrack, JuggleTracker, TrackingConfig
