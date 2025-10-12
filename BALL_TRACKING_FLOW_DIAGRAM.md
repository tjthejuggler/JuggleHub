# Ball Tracking Flow Diagram

This document provides a complete, step-by-step representation of the ball tracking logic in SimpleBallTracker, showing every check and decision made when identifying balls and deciding where to put trackers each frame.

## High-Level Frame Processing Flow

```mermaid
flowchart TD
    Start([Frame Start]) --> Init[Increment frame_counter / Calculate dt]
    Init --> HSV[Convert entire frame to HSV / Cache for performance]
    HSV --> YOLO[Run YOLO Ball Detection]
    YOLO --> Pose[Run Pose Estimation]
    Pose --> HandPersist[Hand Persistence Logic]
    HandPersist --> ResetFlags[Reset has_yolo_detection flags / for all balls]
    ResetFlags --> Override[Override Logic Loop]
    Override --> BallLoop[Ball Update Loop]
    BallLoop --> StateDetect[Detect States and Events]
    StateDetect --> End([Frame End])
```

## Detailed Override Logic (Per Ball)

```mermaid
flowchart TD
    Start([For Each Ball]) --> FindProfile{Find Color<br/>Profile?}
    FindProfile -->|No| Skip[Skip Ball]
    FindProfile -->|Yes| DetLoop[For Each YOLO Detection]
    
    DetLoop --> CalcColor[Calculate color_score / using matchColor]
    CalcColor --> GetThresh[Get class-specific thresholds: / ball: override_ball_* / ball_held: override_ball_held_*]
    
    GetThresh --> CheckConf{confidence >=<br/>threshold?}
    CheckConf -->|No| NextDet[Next Detection]
    CheckConf -->|Yes| CheckColor{color_score >=<br/>threshold?}
    
    CheckColor -->|No| NextDet
    CheckColor -->|Yes| CheckClass{Meets class<br/>requirement?}
    
    CheckClass -->|No| NextDet
    CheckClass -->|Yes| Override[OVERRIDE DETECTED]
    
    Override --> SetPos[Set ball.position = det.world_pos / Set ball.pixel_pos / Set ball.bbox / Set ball.has_yolo_detection = true / Set ball.yolo_confidence / Set ball.yolo_class_id / Set ball.color_match_score]
    
    SetPos --> MarkOverride[Mark ball as overridden / Add to overridden_balls set]
    MarkOverride --> StateUpdate[Update Ball State<br/>Based on Distance]
    
    NextDet --> MoreDet{More<br/>Detections?}
    MoreDet -->|Yes| DetLoop
    MoreDet -->|No| Skip
    
    Skip --> NextBall{More<br/>Balls?}
    NextBall -->|Yes| Start
    NextBall -->|No| End([Override Complete])
```

## Override State Update (Distance-Based)

```mermaid
flowchart TD
    Start([Ball Overridden]) --> CalcDist[Calculate distance to / each visible hand]
    CalcDist --> FindClosest[Find closest_hand_id / and min_dist]
    
    FindClosest --> CheckDist{min_dist < / hand_distance_ / threshold?}
    
    CheckDist -->|No| FarFromHands[Ball far from hands]
    CheckDist -->|Yes| NearHand[Ball near hand]
    
    NearHand --> CheckRecatch{Is immediate / re-catch? / previous_state==IN_FLIGHT / AND same hand / AND less than 3 verified points}
    
    CheckRecatch -->|Yes| PreventRecatch[Set state = IN_FLIGHT / Prevent immediate re-catch]
    CheckRecatch -->|No| CheckPrevState{previous_state / == IN_FLIGHT?}
    
    CheckPrevState -->|No| SetHeld[Set state = HELD / Update held_by_hand_id]
    CheckPrevState -->|Yes| CheckThrowHand{Is throwing / hand? / closest_hand_id == / was_just_thrown_by}
    
    CheckThrowHand -->|Yes| KeepFlight[Keep state = IN_FLIGHT / Clear was_just_thrown_by_hand_id]
    CheckThrowHand -->|No| InitCatch[Clear was_just_thrown_by_hand_id / Call initiateCatch / Generate CATCH event / Set state = HELD]
    
    FarFromHands --> CheckWasHeld{previous_state / == HELD?}
    CheckWasHeld -->|No| SetFlight[Set state = IN_FLIGHT]
    CheckWasHeld -->|Yes| InitThrow[Call initiateThrow / Generate THROW event / Set state = IN_FLIGHT / Add first trajectory point]
    
    PreventRecatch --> AddTraj{state ==<br/>IN_FLIGHT?}
    KeepFlight --> AddTraj
    SetFlight --> AddTraj
    InitThrow --> AddTraj
    SetHeld --> End([State Update Complete])
    InitCatch --> End
    
    AddTraj -->|Yes| AddPoint[Add verified trajectory point / Recalculate prediction if >= 3 points]
    AddTraj -->|No| End
    AddPoint --> End
```

## Ball Update Loop (Non-Overridden Balls)

```mermaid
flowchart TD
    Start([For Each Ball]) --> CheckOverride{Ball in<br/>overridden_balls<br/>set?}
    
    CheckOverride -->|Yes| Skip[Skip - already positioned]
    CheckOverride -->|No| CheckState{ball.state?}
    
    CheckState -->|HELD| UpdateHeld[Call updateHeldBall]
    CheckState -->|IN_FLIGHT| UpdateFlight[Call updateInFlightBall]
    
    UpdateHeld --> NextBall{More<br/>Balls?}
    UpdateFlight --> NextBall
    Skip --> NextBall
    
    NextBall -->|Yes| Start
    NextBall -->|No| End([Ball Loop Complete])
```

## updateHeldBall - Detailed Flow

```mermaid
flowchart TD
    Start([updateHeldBall]) --> FindProfile{Find color<br/>profile?}
    FindProfile -->|No| SetReason[tracking_reason =<br/>NO_COLOR_PROFILE]
    FindProfile -->|Yes| FindHand[Find hand with / id == held_by_hand_id]
    
    FindHand --> CheckHand{Hand found / and visible?}
    CheckHand -->|No| AutoAssign{held_by_hand_id / == -1?}
    
    AutoAssign -->|Yes| FindClosest[Find closest visible hand]
    AutoAssign -->|No| FindAny[Find any visible hand]
    
    FindClosest --> AssignHand[Assign to closest hand]
    FindAny --> CheckAnyFound{Any hand<br/>found?}
    
    CheckAnyFound -->|No| UsePersisted{Any persisted / hands?}
    CheckAnyFound -->|Yes| ForceAssign[Force assign to visible hand]
    
    UsePersisted -->|Yes| UsePersistedPos[Use persisted hand position / Set has_yolo_detection = true / tracking_reason = HELD_persisted_hand_fallback]
    UsePersisted -->|No| KeepLast[tracking_reason = HELD_no_hands_available / Keep last position]
    
    CheckHand -->|Yes| SetWrist[Set ball.position = hand.wrist_pos_3d / Set ball.pixel_pos / Set ball.bbox / Set has_yolo_detection = true / Set yolo_confidence = 0.8 / Set yolo_class_id = 1 / tracking_reason = HELD@wrist]
    
    AssignHand --> SetWrist
    ForceAssign --> SetWrist
    
    SetWrist --> ThrowCheck[Check for THROW]
    ThrowCheck --> DetLoop[For Each YOLO Detection]
    
    DetLoop --> CalcDistHand[Calculate dist_from_hand / Calculate dist_from_ball]
    
    CalcDistHand --> CheckThrowDist{dist_from_hand > / hand_distance_threshold / AND / dist_from_ball < / max_tracker_distance / AND / det.class_id == 0?}
    
    CheckThrowDist -->|No| NextDet[Next Detection]
    CheckThrowDist -->|Yes| CheckThrowColor{color_score > / min_color_match_ / score?}
    
    CheckThrowColor -->|No| NextDet
    CheckThrowColor -->|Yes| CheckMovement{distance_moved >= / hand_distance_threshold / * 0.5?}
    
    CheckMovement -->|No| NextDet
    CheckMovement -->|Yes| ThrowDetected[THROW DETECTED / Call initiateThrow / Generate THROW event / Return]
    
    NextDet --> MoreDet{More<br/>Detections?}
    MoreDet -->|Yes| DetLoop
    MoreDet -->|No| End([updateHeldBall Complete])
    
    SetReason --> End
    KeepLast --> End
    UsePersistedPos --> End
    ThrowDetected --> End
```

## updateInFlightBall - Detailed Flow

```mermaid
flowchart TD
    Start([updateInFlightBall]) --> CheckLockup1{frames_without_ / verified_detection > / 90?}
    
    CheckLockup1 -->|Yes| ForceCatch1[Find nearest hand / Force catch]
    CheckLockup1 -->|No| CheckLockup2{unverified_ / trajectory_points > / 30?}
    
    CheckLockup2 -->|Yes| ForceCatch2[Find nearest hand within 3x max_distance / Force catch or reset trajectory]
    CheckLockup2 -->|No| CheckPoints{verified_point_ / count?}
    
    CheckPoints -->|0| ForceCatch3[CRITICAL: No points / Force catch to nearest hand]
    CheckPoints -->|1| SearchLast[predicted_next = last point / use_prediction = false]
    CheckPoints -->|2| TwoPoint[Use predictWithTwoPoints / use_prediction = true]
    CheckPoints -->|>=3| FullPhysics[Use predictFullTrajectory / use_prediction = true]
    
    SearchLast --> SearchDet[Search for detection]
    TwoPoint --> SearchDet
    FullPhysics --> SearchDet
    
    SearchDet --> CallSearch[Call searchAlongPredictionLine / with search_radius]
    CallSearch --> DetFound{Detection<br/>found?}
    
    DetFound -->|Yes| UseYolo[Use YOLO detection / Set position, bbox, confidence / tracking_reason = IN_FLIGHT_yolo_verified / verified = true]
    DetFound -->|No| ColorBlob[Try color blob search]
    
    ColorBlob --> BlobFound{Color blob / found and / trajectory / consistent?}
    
    BlobFound -->|Yes| UseBlob[Use color blob position / Set has_yolo_detection = true / tracking_reason = IN_FLIGHT_color_blob / verified = true]
    BlobFound -->|No| UsePredicted[Use predicted position / Set has_yolo_detection = true / tracking_reason = IN_FLIGHT_predicted / verified = false]
    
    UseYolo --> AddPoint{verified?}
    UseBlob --> AddPoint
    UsePredicted --> AddPoint
    
    AddPoint -->|Yes| AddVerified[Call addVerifiedPoint / Recalculate prediction if >= 3 points]
    AddPoint -->|No| AddUnverified[Add unverified trajectory point / Check near-hand fallback]
    
    AddVerified --> CheckInvalid{ball.position.z / <= 0?}
    AddUnverified --> CheckInvalid
    
    CheckInvalid -->|Yes| ForceCatch4[Force catch to nearest hand / if within max_tracker_distance]
    CheckInvalid -->|No| CatchCheck[Check for CATCH]
    
    CatchCheck --> CheckCatchState{state == IN_FLIGHT / AND / previous_state == / IN_FLIGHT?}
    
    CheckCatchState -->|No| SafetyCheck[Safety catch check]
    CheckCatchState -->|Yes| CheckMinPoints{verified_point_count / >= 3?}
    
    CheckMinPoints -->|No| SafetyCheck
    CheckMinPoints -->|Yes| CheckMovedAway{Ball moved away / from throw position?}
    
    CheckMovedAway -->|No| SafetyCheck
    CheckMovedAway -->|Yes| FindCatchHand[Find closest hand within threshold / Skip throwing hand if < 10 verified points]
    
    FindCatchHand --> CatchHandFound{Closest hand<br/>found?}
    
    CatchHandFound -->|No| SafetyCheck
    CatchHandFound -->|Yes| CheckThrowingHand{Is throwing / hand? / id == was_just_ / thrown_by}
    
    CheckThrowingHand -->|Yes| PreventCatch[Clear was_just_thrown_by_hand_id / Don't catch]
    CheckThrowingHand -->|No| InitCatch[Clear was_just_thrown_by_hand_id / Call initiateCatch / Generate CATCH event / Return]
    
    SafetyCheck --> SafetyLoop[For each hand]
    SafetyLoop --> CheckSafetyDist{dist_to_hand < / hand_distance_ / threshold?}
    
    CheckSafetyDist -->|Yes| SafetyCatch[SAFETY CATCH / Force catch immediately / Return]
    CheckSafetyDist -->|No| NextHand{More<br/>hands?}
    
    NextHand -->|Yes| SafetyLoop
    NextHand -->|No| End([updateInFlightBall Complete])
    
    PreventCatch --> SafetyCheck
    ForceCatch1 --> End
    ForceCatch2 --> End
    ForceCatch3 --> End
    ForceCatch4 --> End
    InitCatch --> End
    SafetyCatch --> End
```

## initiateThrow - State Transition

```mermaid
flowchart TD
    Start([initiateThrow]) --> StorePos[Store last_held_position / Set was_just_thrown_by_hand_id]
    StorePos --> ClearTraj[Clear trajectory.points / Clear predicted_path / Reset verified_point_count / Set prediction_valid = false]
    ClearTraj --> SetState[Set state = IN_FLIGHT / Set is_held = false]
    SetState --> InitTraj[Set throw_timestamp / Set initial_position / Set gravity / Set search_radius_m]
    InitTraj --> AddFirst[Add first verified trajectory point / Set verified_point_count = 1]
    AddFirst --> ResetVel[Set initial_velocity = 0,0,0 / Will be estimated with 2+ points]
    ResetVel --> GenEvent[Generate THROW event / Add to events vector]
    GenEvent --> End([initiateThrow Complete])
```

## initiateCatch - State Transition

```mermaid
flowchart TD
    Start([initiateCatch]) --> ClearTraj[Clear trajectory.points / Clear predicted_path / Reset verified_point_count / Set prediction_valid = false]
    ClearTraj --> ResetCounters[Reset frames_without_verified_detection / Reset unverified_trajectory_points]
    ResetCounters --> ClearThrow[Clear was_just_thrown_by_hand_id]
    ClearThrow --> ResetPhysics[Reset initial_velocity / Reset initial_position]
    ResetPhysics --> SetState[Set state = HELD / Set is_held = true / Set held_by_hand_id]
    SetState --> SetPos[Set position = hand.wrist_pos_3d]
    SetPos --> GenEvent[Generate CATCH event / Add to events vector]
    GenEvent --> End([initiateCatch Complete])
```

## Key Decision Points Summary

### Override Logic
1. **Color Match**: `color_score >= threshold` (class-specific)
2. **Confidence**: `confidence >= threshold` (class-specific)
3. **Class Requirement**: `!override_require_ball_class || class_id == 0`

### Throw Detection (from HELD)
1. **Distance from Hand**: `dist_from_hand > hand_distance_threshold`
2. **Distance from Ball**: `dist_from_ball < max_tracker_distance_per_frame`
3. **Class Check**: `det.class_id == 0` (must be 'ball', not 'ball_held')
4. **Color Match**: `color_score > min_color_match_score`
5. **Movement**: `distance_moved >= hand_distance_threshold * 0.5`

### Catch Detection (from IN_FLIGHT)
1. **State Check**: `state == IN_FLIGHT && previous_state == IN_FLIGHT`
2. **Minimum Points**: `verified_point_count >= 3`
3. **Moved Away**: `distance_from_throw >= hand_distance_threshold`
4. **Hand Distance**: `dist_to_hand < hand_distance_threshold`
5. **Not Throwing Hand**: `hand.id != was_just_thrown_by_hand_id` (or >= 10 verified points)

### State Determination (Override)
1. **Near Hand**: `min_dist < hand_distance_threshold` → HELD (with re-catch prevention)
2. **Far from Hands**: `min_dist >= hand_distance_threshold` → IN_FLIGHT (generate throw if was HELD)

## Timestamp
Last Updated: 2025-10-12T10:15:00Z