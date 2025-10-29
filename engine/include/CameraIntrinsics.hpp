#pragma once

// Camera intrinsics structure
// Used by multiple components (CameraManager, trackers, visualization)
struct CameraIntrinsics {
    float fx, fy, ppx, ppy;
};