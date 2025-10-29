# SimpleBallTracker 3D - Complete Tracker Placement Decision Flow

This document contains detailed mermaid diagrams showing how the SimpleBallTracker 3D system determines tracker positions for each ball. The flow is broken into sections for readability.

---

## 1. Frame Initialization & Override Check

```mermaid
flowchart TD
    Start([Frame Update Start]) --> FrameInit[Increment frame_counter_<br/>Calculate dt]
    FrameInit --> CacheHSV[Convert entire frame to HSV once<br/>Cache for performance]
    CacheHSV --> Preprocess[Preprocess frame for YOLO<br/>640x640, normalize]
    
    Preprocess --> RunYOLO[Run Ball Detection YOLO<br/>Get raw detections]
    RunYOLO --> RunPose[Run Pose Estimation YOLO<br/>Get hand keypoints]
    
    RunPose --> UpdateHandVel[Update Hand Velocity<br/>Track position history<br/>Calculate velocity vectors]
    UpdateHandVel --> HandPersist[Hand Persistence<br/>Fill missing hands with<br/>last known positions]
    
    HandPersist --> IncrementFlight[Increment frames_in_flight_since_throw<br/>for ALL IN_FLIGHT balls]
    IncrementFlight --> ResetFlags[Reset has_yolo_detection flags<br/>for all balls]
    
    ResetFlags --> OverrideCheck{Check Override<br/>for each ball}
    
    OverrideCheck --> ForEachDet[For each YOLO detection]
    ForEachDet --> CalcColorScore[Calculate color_score<br/>using matchColor]
    CalcColorScore --> CheckOverride{Meets Override Criteria?<br/>confidence >= threshold<br/>color_score >= threshold<br/>class_id == 0 OR ignore_class}
    
    CheckOverride -->|YES| OverrideForce[OVERRIDE DETECTED<br/>Force ball to detection<br/>Set position, bbox, confidence<br/>Mark as overridden]
    CheckOverride -->|NO| NextDetection{More<br/>detections?}
    NextDetection -->|YES| ForEachDet
    NextDetection -->|NO| ToStateCheck[Continue to<br/>State-Based Update]
    
    OverrideForce --> OverrideStateUpdate[See Override<br/>State Update diagram]
    
    classDef override fill:#ff9999
    class OverrideForce,OverrideStateUpdate override
```

---

## 2. Override State Update (Distance-Based)

```mermaid
flowchart TD
    Start([Override Detected]) --> CalcDist[Calculate distance<br/>to all hands]
    CalcDist --> FindClosest[Find closest hand]
    
    FindClosest --> CheckDist{Distance <<br/>hand_distance_threshold?}
    
    CheckDist -->|YES - Near Hand| WasInFlight{Was ball<br/>IN_FLIGHT?}
    
    WasInFlight -->|YES| CheckCooldown{Check Cooldown:<br/>last_throwing_hand_id<br/>== closest_hand?}
    
    CheckCooldown -->|YES - Same Hand| CheckFrames{frames_in_flight<br/>>= min_frames_before_catch?}
    
    CheckFrames -->|NO - Too Soon| BlockCatch[CATCH BLOCKED<br/>Keep state = IN_FLIGHT<br/>Continue to next ball]
    
    CheckFrames -->|YES - Enough Frames| AllowCatch[Catch Allowed]
    CheckCooldown -->|NO - Different Hand| AllowCatch
    
    AllowCatch --> CheckDiffHand{Catching hand<br/>!= throwing hand?}
    CheckDiffHand -->|YES| InitCatch[initiateCatch<br/>Clear trajectory<br/>Clear last_throwing_hand_id<br/>Set state = HELD<br/>Generate CATCH event]
    CheckDiffHand -->|NO| SetInFlight1[Set state = IN_FLIGHT]
    
    WasInFlight -->|NO - Was HELD| SetHeld[Set state = HELD<br/>held_by_hand_id = closest_hand]
    
    CheckDist -->|NO - Far From Hands| WasHeld{Was ball<br/>HELD?}
    
    WasHeld -->|YES| InitThrow[initiateThrow<br/>Clear trajectory<br/>Add first point<br/>Set last_throwing_hand_id<br/>Reset frames_in_flight_since_throw<br/>Generate THROW event]
    
    WasHeld -->|NO| SetInFlight2[Set state = IN_FLIGHT]
    
    InitThrow --> AddTrajPoint[Add trajectory point<br/>Recalculate prediction]
    SetInFlight1 --> AddTrajPoint
    SetInFlight2 --> AddTrajPoint
    
    AddTrajPoint --> Done([Skip normal update<br/>Continue to next ball])
    InitCatch --> Done
    SetHeld --> Done
    BlockCatch --> Done
    
    classDef override fill:#ff9999
    classDef catch fill:#ffcc99
    classDef throw fill:#ff99ff
    
    class Start,CalcDist,FindClosest override
    class InitCatch,AllowCatch catch
    class InitThrow throw
```

---

## 3. HELD Ball Update - Position & Hand Assignment

```mermaid
flowchart TD
    Start([updateHeldBall]) --> FindProfile[Find ColorProfile<br/>for ball.color_name]
    
    FindProfile --> FindHand{Find hand with<br/>held_by_hand_id}
    
    FindHand -->|NOT FOUND| CheckNeverAssigned{held_by_hand_id<br/>== -1?}
    
    CheckNeverAssigned -->|YES - Never Assigned| FindClosest[Find closest visible hand<br/>Calculate distances]
    FindClosest --> AssignClosest[Assign to closest hand<br/>held_by_hand_id = closest_hand.id]
    
    CheckNeverAssigned -->|NO - Hand Missing| FindAnyHand{Any visible<br/>hand?}
    FindAnyHand -->|YES| ForceAssign[FORCE assign to<br/>any visible hand]
    FindAnyHand -->|NO| CheckPersisted{Any persisted<br/>hands?}
    
    CheckPersisted -->|YES| UsePersistedPos[Use persisted hand position<br/>Set bbox, has_yolo_detection=true<br/>tracking_reason = persisted_fallback]
    CheckPersisted -->|NO| KeepLastPos[Keep last position<br/>tracking_reason = no_hands_available]
    
    FindHand -->|FOUND| SetWristPos[Set ball.position = hand.wrist_pos_3d<br/>Set ball.pixel_pos via projection<br/>Set bbox size = 30px<br/>has_yolo_detection = true<br/>yolo_confidence = 0.8<br/>yolo_class_id = 1<br/>tracking_reason = HELD@wrist]
    
    AssignClosest --> SetWristPos
    ForceAssign --> SetWristPos
    
    SetWristPos --> CheckVelEnabled{Hand velocity<br/>enabled?}
    
    CheckVelEnabled -->|YES| CheckHandSpeed{hand.has_valid_velocity<br/>AND hand_speed ><br/>velocity_threshold?}
    
    CheckHandSpeed -->|YES| CalcVelZone[Calculate predicted_hand_pos<br/>= hand.wrist + velocity * dt<br/>Set hand_velocity_active = true<br/>Set detection zone center & radius]
    
    CheckHandSpeed -->|NO| NoVelZone[hand_velocity_active = false]
    CheckVelEnabled -->|NO| NoVelZone
    
    CalcVelZone --> ToThrowCheck[Continue to<br/>Throw Detection]
    NoVelZone --> ToThrowCheck
    UsePersistedPos --> End([HELD update complete])
    KeepLastPos --> End
    
    classDef held fill:#99ccff
    class Start,FindProfile,FindHand,SetWristPos,CheckVelEnabled held
```

---

## 4. HELD Ball Update - Throw Detection

```mermaid
flowchart TD
    Start([For each YOLO detection]) --> CalcDist[Calculate dist_from_hand<br/>Calculate dist_from_ball]
    
    CalcDist --> CheckInVelZone{In velocity zone?<br/>dist from predicted_hand_pos<br/>< detection_radius}
    
    CheckInVelZone -->|YES| ReduceThresh[Reduce confidence threshold<br/>Reduce color threshold<br/>Optionally ignore class requirement]
    
    ReduceThresh --> CheckAggressive{dist_from_wrist ><br/>velocity_distance_reduction?}
    
    CheckAggressive -->|YES| AggressiveMode[AGGRESSIVE THROW MODE<br/>is_velocity_zone_throw = true<br/>Extra-reduce color threshold<br/>50% of normal or min 0.15]
    
    CheckInVelZone -->|NO| NormalThresh[Use normal thresholds]
    CheckAggressive -->|NO| StandardCheck
    
    NormalThresh --> StandardCheck{Standard Throw Check:<br/>dist_from_hand > effective_threshold<br/>AND dist_from_ball < max_distance<br/>AND meets_class_requirement}
    
    StandardCheck -->|YES| CalcColor[Calculate color_score<br/>using matchColor]
    AggressiveMode --> CalcColor
    
    CalcColor --> CheckThresh{color_score > threshold<br/>AND confidence > threshold?}
    
    CheckThresh -->|YES| CheckMovement{distance_moved ><br/>min_movement_threshold?<br/>min = effective_threshold * 0.5}
    
    CheckMovement -->|YES| ThrowDetected[THROW DETECTED<br/>Set last_throwing_hand_id = hand.id<br/>Reset frames_in_flight_since_throw = 0]
    
    ThrowDetected --> InitThrow[initiateThrow<br/>Clear trajectory<br/>Add first verified point<br/>Set throw_timestamp<br/>Generate THROW event]
    
    CheckMovement -->|NO| NextDet{More<br/>detections?}
    CheckThresh -->|NO| NextDet
    StandardCheck -->|NO| NextDet
    
    NextDet -->|YES| Start
    NextDet -->|NO| NoThrow([No throw detected<br/>Ball stays HELD])
    
    InitThrow --> Done([HELD update complete<br/>Ball now IN_FLIGHT])
    
    classDef held fill:#99ccff
    classDef throw fill:#ff99ff
    
    class Start,CalcDist,CheckInVelZone held
    class ThrowDetected,InitThrow,Done throw
```

---

## 5. IN_FLIGHT Ball Update - Lockup Prevention & Prediction

```mermaid
flowchart TD
    Start([updateInFlightBall]) --> CheckLockup1{frames_without_verified<br/>> MAX_FRAMES?<br/>default: 90}
    
    CheckLockup1 -->|YES| ForceCatch1[LOCKUP PREVENTION<br/>Force catch to nearest hand<br/>Generate CATCH event]
    
    CheckLockup1 -->|NO| CheckLockup2{unverified_points<br/>> MAX_UNVERIFIED?<br/>default: 30}
    
    CheckLockup2 -->|YES| CheckHandDist{nearest_hand_dist <<br/>max_tracker_distance * 3?}
    CheckHandDist -->|YES| ForceCatch2[LOCKUP PREVENTION<br/>Force catch to nearest hand]
    CheckHandDist -->|NO| ResetTraj1[Reset trajectory<br/>Keep last position]
    
    CheckLockup2 -->|NO| CheckPoints{verified_point_count?}
    
    CheckPoints -->|0 points| CheckHands{Any hands<br/>available?}
    CheckHands -->|YES| ForceCatch3[CRITICAL FALLBACK<br/>Force catch to nearest hand]
    CheckHands -->|NO| KeepPos[Keep last position<br/>tracking_reason = no_points_no_hands]
    
    CheckPoints -->|1 point| UseLastPos[predicted_next = last position<br/>use_prediction = false<br/>search_radius = 0.30m]
    
    CheckPoints -->|2 points| TwoPointPred[predictWithTwoPoints<br/>Use parabolic fit<br/>Linear + acceleration<br/>use_prediction = true]
    
    CheckPoints -->|3+ points| FullPred[predictFullTrajectory<br/>Full physics-based<br/>Parabolic trajectory<br/>use_prediction = true]
    
    UseLastPos --> SearchDet[searchAlongPredictionLine<br/>Check YOLO detections<br/>within search_radius]
    TwoPointPred --> SearchDet
    FullPred --> SearchDet
    
    SearchDet --> ToDetectionCheck[Continue to<br/>Detection Verification]
    
    ForceCatch1 --> End([IN_FLIGHT update complete])
    ForceCatch2 --> End
    ForceCatch3 --> End
    ResetTraj1 --> End
    KeepPos --> End
    
    classDef flight fill:#99ff99
    classDef lockup fill:#ff6666
    
    class Start,CheckPoints,SearchDet flight
    class CheckLockup1,CheckLockup2,ForceCatch1,ForceCatch2,ForceCatch3 lockup
```

---

## 6. IN_FLIGHT Ball Update - Detection Verification

```mermaid
flowchart TD
    Start([Detection Search Result]) --> CheckYOLO{YOLO detection<br/>found along<br/>prediction line?}
    
    CheckYOLO -->|YES| VerifyYOLO[Set ball.position = det.world_pos<br/>Set pixel_pos, bbox<br/>has_yolo_detection = true<br/>yolo_confidence = det.confidence<br/>tracking_reason = IN_FLIGHT_yolo_verified<br/>verified = TRUE]
    
    CheckYOLO -->|NO| ColorBlob[searchForColorBlob<br/>in predicted area<br/>using GPU-accelerated search]
    
    ColorBlob --> CheckBlob{Color blob<br/>found?}
    
    CheckBlob -->|YES| CheckTrajConsist{Trajectory consistent?<br/>Check direction similarity<br/>Check speed ratio<br/>0.3x to 3.0x expected}
    
    CheckTrajConsist -->|YES| VerifyBlob[Set ball.position = blob_3d<br/>Set pixel_pos, bbox<br/>has_yolo_detection = true<br/>yolo_confidence = 0.6<br/>tracking_reason = IN_FLIGHT_color_blob<br/>verified = TRUE]
    
    CheckTrajConsist -->|NO| UsePredicted[Set ball.position = predicted_next<br/>Set pixel_pos via projection<br/>has_yolo_detection = true<br/>yolo_confidence = 0.4<br/>tracking_reason = IN_FLIGHT_predicted<br/>verified = FALSE]
    
    CheckBlob -->|NO| UsePredicted
    
    VerifyYOLO --> AddVerified[addVerifiedPoint<br/>Add to trajectory.points<br/>Increment verified_point_count<br/>Recalculate prediction<br/>Reset frames_without_verified = 0]
    
    VerifyBlob --> AddVerified
    
    UsePredicted --> AddUnverified[Add UNVERIFIED point<br/>Don't increment verified_count<br/>Increment unverified_trajectory_points<br/>Increment frames_without_verified]
    
    AddVerified --> ToCatchCheck[Continue to<br/>Catch Detection]
    AddUnverified --> CheckNearHand{Ball very close<br/>to any hand?<br/>dist < 0.15m}
    
    CheckNearHand -->|YES| CheckCooldown1{Check Cooldown:<br/>last_throwing_hand_id<br/>== hand.id?}
    
    CheckCooldown1 -->|YES| CheckFrames1{frames_in_flight<br/>>= min_frames_before_catch?}
    CheckFrames1 -->|NO| BlockCatch1[Block Catch<br/>Continue flight]
    CheckFrames1 -->|YES| NearHandCatch[NEAR-HAND FALLBACK<br/>initiateCatch]
    
    CheckCooldown1 -->|NO| NearHandCatch
    CheckNearHand -->|NO| ToCatchCheck
    
    NearHandCatch --> End([IN_FLIGHT update complete])
    BlockCatch1 --> End
    
    classDef flight fill:#99ff99
    classDef catch fill:#ffcc99
    
    class Start,CheckYOLO,VerifyYOLO,VerifyBlob,AddVerified flight
    class NearHandCatch catch
```

---

## 7. IN_FLIGHT Ball Update - Catch Detection

```mermaid
flowchart TD
    Start([Catch Detection Check]) --> CheckInvalid{ball.position.z<br/><= 0?}
    
    CheckInvalid -->|YES| CheckDist{nearest_hand_dist <<br/>max_tracker_distance?}
    
    CheckDist -->|YES| CheckCooldown1{Check Cooldown}
    CheckCooldown1 -->|PASS| CriticalCatch[CRITICAL FALLBACK<br/>initiateCatch<br/>Invalid position recovery]
    CheckCooldown1 -->|FAIL| KeepPos1[Keep predicted position<br/>tracking_reason = fallback_blocked]
    
    CheckDist -->|NO| KeepPos2[Keep predicted position<br/>tracking_reason = fallback_too_far]
    
    CheckInvalid -->|NO| CheckMainCatch{Ball IN_FLIGHT<br/>AND previous IN_FLIGHT<br/>AND verified_points >= 3<br/>AND moved away from throw?}
    
    CheckMainCatch -->|YES| FindClosest[Find CLOSEST hand<br/>within hand_distance_threshold]
    
    FindClosest --> CheckFound{Hand<br/>found?}
    
    CheckFound -->|YES| CheckGlobalRule{GLOBAL 3-FRAME RULE:<br/>last_throwing_hand_id<br/>== closest_hand?}
    
    CheckGlobalRule -->|YES - Same Hand| CheckFrames{frames_in_flight<br/>>= min_frames_before_catch?<br/>default: 3}
    
    CheckFrames -->|NO - Too Soon| BlockCatch2[CATCH BLOCKED<br/>Same hand that threw<br/>Not enough frames passed<br/>Continue flight]
    
    CheckFrames -->|YES - Enough Frames| AllowCatch[Catch Allowed]
    
    CheckGlobalRule -->|NO - Different Hand| CheckLegacy{Legacy check:<br/>was_just_thrown_by<br/>== closest_hand?}
    
    CheckLegacy -->|YES| BlockCatch3[Block Catch<br/>Clear was_just_thrown_by<br/>Continue flight]
    
    CheckLegacy -->|NO| AllowCatch
    
    AllowCatch --> MainCatch[MAIN CATCH DETECTION<br/>initiateCatch<br/>Clear trajectory<br/>Clear last_throwing_hand_id<br/>Clear was_just_thrown_by<br/>Generate CATCH event]
    
    CheckFound -->|NO| SafetyCheck
    CheckMainCatch -->|NO| SafetyCheck{SAFETY CHECK:<br/>Ball still IN_FLIGHT<br/>AND within threshold<br/>of any hand?}
    
    SafetyCheck -->|YES| CheckCooldown2{Check Cooldown}
    CheckCooldown2 -->|PASS| SafetyCatch[SAFETY CATCH<br/>initiateCatch<br/>Prevent stuck IN_FLIGHT]
    CheckCooldown2 -->|FAIL| FlightComplete
    
    SafetyCheck -->|NO| FlightComplete([IN_FLIGHT update complete<br/>Ball stays in flight])
    
    CriticalCatch --> End([IN_FLIGHT update complete])
    MainCatch --> End
    SafetyCatch --> End
    BlockCatch2 --> End
    BlockCatch3 --> End
    KeepPos1 --> End
    KeepPos2 --> End
    
    classDef flight fill:#99ff99
    classDef catch fill:#ffcc99
    
    class Start,CheckInvalid,CheckMainCatch,FindClosest flight
    class CriticalCatch,MainCatch,SafetyCatch,AllowCatch catch
```

---

## Summary: Tracker Placement Priority

### For Each Ball, Per Frame:

1. **Override Check** (Highest Priority)
   - If detection meets high confidence + color + class requirements
   - Force ball to detection position immediately
   - Update state based on distance to hands (not YOLO class)

2. **HELD Ball** (if not overridden)
   - Position: Always at hand wrist
   - Check for throw: Detection moving away from hand
   - Velocity zone: Reduced thresholds if hand moving fast

3. **IN_FLIGHT Ball** (if not overridden)
   - Lockup prevention checks first
   - Prediction based on point count (0/1/2/3+)
   - Detection priority: YOLO → Color blob → Predicted
   - Multiple catch detection paths with cooldown

4. **Cooldown System** (GLOBAL 3-FRAME RULE)
   - Applies to ALL catch detection paths
   - Prevents same-hand re-catch for 3 frames
   - Uses `last_throwing_hand_id` + `frames_in_flight_since_throw`

---

*Generated: 2025-10-14*
*Last Updated: 2025-10-14 12:18 UTC*