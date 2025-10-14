#!/usr/bin/env python3
"""
test_openvino_models.py
Purpose: Benchmark OpenVINO models using OpenVINO Python API
This directly matches how the C++ engine uses OpenVINO models.
"""

import time
import numpy as np
import cv2
from pathlib import Path
from openvino.runtime import Core

# --- Configuration ---------------------------------------------------
# These paths match what the C++ engine uses
BALL_MODEL_XML = "/home/twain/Projects/JuggleHub/engine/models/V8_balls_held/yolo11n.xml"
POSE_MODEL_XML = "/home/twain/Projects/JuggleHub/engine/models/yoloposenano/yolo11n-pose.xml"
TEST_IMAGE_PATH = "/home/twain/Projects/JuggleHub/engine/data/3_training_datasets/V9_1_1_targeted/valid/images/rs455_2025-09-05_13-12-49_frame_149.jpg"

# Model input size (YOLO11 uses 640x640)
INPUT_WIDTH = 640
INPUT_HEIGHT = 640
# ---------------------------------------------------------------------


def preprocess_image(image_path: str, width: int = INPUT_WIDTH, height: int = INPUT_HEIGHT):
    """
    Preprocess image for YOLO model inference.
    Matches the preprocessing done in SimpleBallTracker::preprocess()
    
    Args:
        image_path: Path to input image
        width: Target width
        height: Target height
    
    Returns:
        Preprocessed image as numpy array in NCHW format
    """
    # Read image
    img = cv2.imread(image_path)
    if img is None:
        raise ValueError(f"Could not read image: {image_path}")
    
    # Resize to model input size
    img_resized = cv2.resize(img, (width, height))
    
    # Convert to float32 and normalize to [0, 1]
    img_float = img_resized.astype(np.float32) / 255.0
    
    # Convert from HWC to CHW format (channels first)
    img_chw = np.transpose(img_float, (2, 0, 1))
    
    # Add batch dimension: CHW -> NCHW
    img_nchw = np.expand_dims(img_chw, axis=0)
    
    return img_nchw


def benchmark_openvino_model(
    model_xml_path: str,
    model_name: str,
    image_path: str,
    device: str = "GPU",
    num_iterations: int = 100
):
    """
    Benchmark an OpenVINO model.
    This matches how SimpleBallTracker loads and uses models in C++.
    
    Args:
        model_xml_path: Path to the .xml model file
        model_name: Descriptive name for the model
        image_path: Path to test image
        device: OpenVINO device ('CPU', 'GPU', 'AUTO')
        num_iterations: Number of iterations for benchmarking
    """
    print("-" * 60)
    print(f"🚀 Benchmarking: {model_name}")
    print(f"   Model: {Path(model_xml_path).name}")
    print(f"   Device: {device}")
    
    try:
        # Step 1: Initialize OpenVINO Core (matches: ov::Core core_)
        print("   [1/6] Initializing OpenVINO Core...")
        core = Core()
        
        # List available devices
        available_devices = core.available_devices
        print(f"   Available devices: {available_devices}")
        
        # Check if requested device is available
        if device not in available_devices and device != "AUTO":
            print(f"   ⚠️  Warning: {device} not available, falling back to CPU")
            device = "CPU"
        
        # Step 2: Read model (matches: core_.read_model())
        print(f"   [2/6] Reading model from {model_xml_path}...")
        model = core.read_model(model=model_xml_path)
        
        # Step 3: Compile model for device (matches: core_.compile_model())
        print(f"   [3/6] Compiling model for {device}...")
        compiled_model = core.compile_model(model=model, device_name=device)
        
        # Step 4: Create inference request (matches: model_.create_infer_request())
        print("   [4/6] Creating inference request...")
        infer_request = compiled_model.create_infer_request()
        
        # Get input/output info
        input_layer = compiled_model.input(0)
        output_layer = compiled_model.output(0)
        input_shape = input_layer.shape
        output_shape = output_layer.shape
        
        print(f"   Input shape: {input_shape}")
        print(f"   Output shape: {output_shape}")
        
        # Step 5: Preprocess image (matches: SimpleBallTracker::preprocess())
        print("   [5/6] Preprocessing image...")
        input_data = preprocess_image(image_path)
        
        # Warm-up run
        print("   Performing warm-up run...")
        infer_request.infer({input_layer: input_data})
        
        # Step 6: Benchmark inference (matches: infer_.infer())
        print(f"   [6/6] Running benchmark ({num_iterations} iterations)...")
        start_time = time.perf_counter()
        
        for _ in range(num_iterations):
            infer_request.infer({input_layer: input_data})
            
        end_time = time.perf_counter()
        
        # Calculate statistics
        total_time = end_time - start_time
        avg_time_per_frame = total_time / num_iterations
        true_fps = 1 / avg_time_per_frame
        
        print(f"\n✅ Benchmark Complete!")
        print(f"   Device: {device}")
        print(f"   Total time: {total_time:.2f} s")
        print(f"   Average time per frame: {avg_time_per_frame * 1000:.2f} ms")
        print(f"   Throughput: {true_fps:.2f} FPS")
        
        # Get output for verification
        output_tensor = infer_request.get_output_tensor()
        output_data = output_tensor.data
        print(f"   Output data shape: {output_data.shape}")
        
    except Exception as e:
        print(f"\n❌ Error occurred: {e}")
        import traceback
        traceback.print_exc()
    
    print("-" * 60 + "\n")


def benchmark_combined_models(
    ball_model_xml_path: str,
    pose_model_xml_path: str,
    image_path: str,
    device: str = "GPU",
    num_iterations: int = 100
):
    """
    Benchmark both models running together (ball + pose detection).
    This simulates the actual engine workload where both models run per frame.
    
    Args:
        ball_model_xml_path: Path to ball detection .xml model file
        pose_model_xml_path: Path to pose detection .xml model file
        image_path: Path to test image
        device: OpenVINO device ('CPU', 'GPU', 'AUTO')
        num_iterations: Number of iterations for benchmarking
    """
    print("-" * 60)
    print(f"🚀 Benchmarking: Combined Ball + Pose Detection")
    print(f"   Ball Model: {Path(ball_model_xml_path).name}")
    print(f"   Pose Model: {Path(pose_model_xml_path).name}")
    print(f"   Device: {device}")
    
    try:
        # Step 1: Initialize OpenVINO Core
        print("   [1/8] Initializing OpenVINO Core...")
        core = Core()
        
        # List available devices
        available_devices = core.available_devices
        print(f"   Available devices: {available_devices}")
        
        # Check if requested device is available
        if device not in available_devices and device != "AUTO":
            print(f"   ⚠️  Warning: {device} not available, falling back to CPU")
            device = "CPU"
        
        # Step 2: Read ball detection model
        print(f"   [2/8] Reading ball detection model...")
        ball_model = core.read_model(model=ball_model_xml_path)
        
        # Step 3: Read pose detection model
        print(f"   [3/8] Reading pose detection model...")
        pose_model = core.read_model(model=pose_model_xml_path)
        
        # Step 4: Compile both models for device
        print(f"   [4/8] Compiling ball model for {device}...")
        ball_compiled = core.compile_model(model=ball_model, device_name=device)
        
        print(f"   [5/8] Compiling pose model for {device}...")
        pose_compiled = core.compile_model(model=pose_model, device_name=device)
        
        # Step 5: Create inference requests
        print("   [6/8] Creating inference requests...")
        ball_infer = ball_compiled.create_infer_request()
        pose_infer = pose_compiled.create_infer_request()
        
        # Get input/output info
        ball_input = ball_compiled.input(0)
        pose_input = pose_compiled.input(0)
        
        print(f"   Ball model input shape: {ball_input.shape}")
        print(f"   Pose model input shape: {pose_input.shape}")
        
        # Step 6: Preprocess image
        print("   [7/8] Preprocessing image...")
        input_data = preprocess_image(image_path)
        
        # Warm-up runs
        print("   Performing warm-up runs...")
        ball_infer.infer({ball_input: input_data})
        pose_infer.infer({pose_input: input_data})
        
        # Step 7: Benchmark combined inference
        print(f"   [8/8] Running combined benchmark ({num_iterations} iterations)...")
        start_time = time.perf_counter()
        
        for _ in range(num_iterations):
            # Run both models sequentially (as the engine does)
            ball_infer.infer({ball_input: input_data})
            pose_infer.infer({pose_input: input_data})
            
        end_time = time.perf_counter()
        
        # Calculate statistics
        total_time = end_time - start_time
        avg_time_per_frame = total_time / num_iterations
        combined_fps = 1 / avg_time_per_frame
        
        print(f"\n✅ Combined Benchmark Complete!")
        print(f"   Device: {device}")
        print(f"   Total time: {total_time:.2f} s")
        print(f"   Average time per frame: {avg_time_per_frame * 1000:.2f} ms")
        print(f"   Estimated FPS (both models): {combined_fps:.2f} FPS")
        print(f"\n   ℹ️  This represents the realistic FPS when running")
        print(f"      both ball detection and pose detection together,")
        print(f"      as the engine does in production.")
        
    except Exception as e:
        print(f"\n❌ Error occurred: {e}")
        import traceback
        traceback.print_exc()
    
    print("-" * 60 + "\n")


def benchmark_full_pipeline(
    ball_model_xml_path: str,
    pose_model_xml_path: str,
    image_path: str,
    device: str = "GPU",
    num_iterations: int = 100
):
    """
    Benchmark the full Simple2DBallTracker pipeline with detailed profiling.
    This breaks down the time spent in each processing step to identify bottlenecks.
    
    Args:
        ball_model_xml_path: Path to ball detection .xml model file
        pose_model_xml_path: Path to pose detection .xml model file
        image_path: Path to test image
        device: OpenVINO device ('CPU', 'GPU', 'AUTO')
        num_iterations: Number of iterations for benchmarking
    """
    print("-" * 60)
    print(f"🔍 Benchmarking: Full Pipeline with Detailed Profiling")
    print(f"   Ball Model: {Path(ball_model_xml_path).name}")
    print(f"   Pose Model: {Path(pose_model_xml_path).name}")
    print(f"   Device: {device}")
    
    try:
        # Initialize OpenVINO Core
        print("   [1/9] Initializing OpenVINO Core...")
        core = Core()
        
        # Check device availability
        available_devices = core.available_devices
        if device not in available_devices and device != "AUTO":
            print(f"   ⚠️  Warning: {device} not available, falling back to CPU")
            device = "CPU"
        
        # Load and compile models
        print(f"   [2/9] Reading and compiling models...")
        ball_model = core.read_model(model=ball_model_xml_path)
        pose_model = core.read_model(model=pose_model_xml_path)
        ball_compiled = core.compile_model(model=ball_model, device_name=device)
        pose_compiled = core.compile_model(model=pose_model, device_name=device)
        
        # Create inference requests
        print("   [3/9] Creating inference requests...")
        ball_infer = ball_compiled.create_infer_request()
        pose_infer = pose_compiled.create_infer_request()
        
        ball_input = ball_compiled.input(0)
        pose_input = pose_compiled.input(0)
        
        # Load test image
        print("   [4/9] Loading test image...")
        img = cv2.imread(image_path)
        if img is None:
            raise ValueError(f"Could not read image: {image_path}")
        
        # Warm-up runs
        print("   [5/9] Performing warm-up runs...")
        input_data = preprocess_image(image_path)
        ball_infer.infer({ball_input: input_data})
        pose_infer.infer({pose_input: input_data})
        
        # Initialize timing accumulators
        time_preprocess_ball = 0.0
        time_ball_inference = 0.0
        time_ball_postprocess = 0.0
        time_preprocess_pose = 0.0
        time_pose_inference = 0.0
        time_pose_postprocess = 0.0
        time_tracking = 0.0
        time_conversion = 0.0
        
        print(f"   [6/9] Running detailed profiling ({num_iterations} iterations)...")
        
        for iteration in range(num_iterations):
            # === SHARED PREPROCESSING (OPTIMIZATION) ===
            # Both models use the same input size and preprocessing
            # So we only preprocess once and reuse for both models
            
            t0 = time.perf_counter()
            preprocessed = preprocess_image(image_path)
            t1 = time.perf_counter()
            time_preprocess_ball += (t1 - t0)
            
            # === BALL DETECTION PIPELINE ===
            
            # 1. Ball inference (using shared preprocessed image)
            t0 = time.perf_counter()
            ball_infer.infer({ball_input: preprocessed})
            t1 = time.perf_counter()
            time_ball_inference += (t1 - t0)
            
            # 2. Ball postprocessing (parse YOLO output, NMS)
            t0 = time.perf_counter()
            output_tensor = ball_infer.get_output_tensor()
            output_data = output_tensor.data
            
            # Simulate the postprocessing done in Simple2DBallTracker::runBallDetection
            num_channels = 4 + 2  # 4 bbox coords + 2 classes
            output_buffer = np.array(output_data).reshape(num_channels, -1).T
            
            # Parse detections (simplified - just iterate through)
            boxes = []
            confidences = []
            for i in range(min(100, output_buffer.shape[0])):  # Check first 100 detections
                class_scores = output_buffer[i, 4:]
                max_score = np.max(class_scores)
                if max_score > 0.25:
                    cx, cy, w, h = output_buffer[i, :4]
                    boxes.append([cx, cy, w, h])
                    confidences.append(max_score)
            
            # NMS (simplified)
            if len(boxes) > 0:
                boxes_array = np.array(boxes)
                confidences_array = np.array(confidences)
                # Simulate NMS overhead
                _ = cv2.dnn.NMSBoxes(boxes_array.tolist(), confidences_array.tolist(), 0.25, 0.45)
            
            t1 = time.perf_counter()
            time_ball_postprocess += (t1 - t0)
            
            # === POSE DETECTION PIPELINE ===
            
            # 3. Pose inference (reusing shared preprocessed image)
            t0 = time.perf_counter()
            pose_infer.infer({pose_input: preprocessed})
            t1 = time.perf_counter()
            time_pose_inference += (t1 - t0)
            
            # 4. Pose postprocessing (parse keypoints)
            t0 = time.perf_counter()
            output_tensor = pose_infer.get_output_tensor()
            output_data = output_tensor.data
            
            # Simulate the postprocessing done in Simple2DBallTracker::runPoseEstimation
            output_shape = output_tensor.shape
            if len(output_shape) >= 3:
                num_channels = output_shape[1]
                num_detections = output_shape[2]
                output_buffer = np.array(output_data).reshape(num_channels, num_detections).T
                
                # Parse keypoints for first person
                if output_buffer.shape[0] > 0:
                    person_conf = output_buffer[0, 4]
                    if person_conf > 0.3:
                        # Extract 17 keypoints (3 values each)
                        for kp_idx in range(17):
                            base_idx = 5 + kp_idx * 3
                            if base_idx + 2 < output_buffer.shape[1]:
                                kp_x = output_buffer[0, base_idx + 0]
                                kp_y = output_buffer[0, base_idx + 1]
                                kp_conf = output_buffer[0, base_idx + 2]
            
            t1 = time.perf_counter()
            time_pose_postprocess += (t1 - t0)
            
            # === TRACKING & CONVERSION ===
            
            # 5. Simple tracking (findClosestBallId)
            t0 = time.perf_counter()
            # Simulate nearest-neighbor tracking
            if len(boxes) > 0:
                for box in boxes[:5]:  # Track up to 5 balls
                    # Calculate distances (simplified)
                    distances = [np.sqrt((box[0] - b[0])**2 + (box[1] - b[1])**2) for b in boxes]
                    min_dist = min(distances) if distances else 0
            t1 = time.perf_counter()
            time_tracking += (t1 - t0)
            
            # 6. Convert Simple2DBall to SimpleBall format
            t0 = time.perf_counter()
            # Simulate the conversion loop in update()
            balls_3d = []
            for _ in range(len(boxes)):
                ball = {
                    'id': 0,
                    'pixel_pos': (0, 0),
                    'bbox': (0, 0, 0, 0),
                    'has_yolo_detection': True,
                    'position': (0, 0, 0),
                    'color_name': 'unknown'
                }
                balls_3d.append(ball)
            t1 = time.perf_counter()
            time_conversion += (t1 - t0)
        
        # Calculate statistics
        print(f"   [7/9] Calculating statistics...")
        
        # Note: time_preprocess_pose is now 0 since we only preprocess once
        total_time = (time_preprocess_ball + time_ball_inference + time_ball_postprocess +
                     time_pose_inference + time_pose_postprocess +
                     time_tracking + time_conversion)
        
        avg_time_per_frame = total_time / num_iterations
        pipeline_fps = 1 / avg_time_per_frame
        
        # Calculate individual component times
        avg_preprocess_ball = (time_preprocess_ball / num_iterations) * 1000
        avg_ball_inference = (time_ball_inference / num_iterations) * 1000
        avg_ball_postprocess = (time_ball_postprocess / num_iterations) * 1000
        avg_preprocess_pose = (time_preprocess_pose / num_iterations) * 1000
        avg_pose_inference = (time_pose_inference / num_iterations) * 1000
        avg_pose_postprocess = (time_pose_postprocess / num_iterations) * 1000
        avg_tracking = (time_tracking / num_iterations) * 1000
        avg_conversion = (time_conversion / num_iterations) * 1000
        
        # Calculate FPS impact of each component
        fps_loss_preprocess_ball = 1 / (avg_preprocess_ball / 1000) if avg_preprocess_ball > 0 else 0
        fps_loss_ball_inference = 1 / (avg_ball_inference / 1000) if avg_ball_inference > 0 else 0
        fps_loss_ball_postprocess = 1 / (avg_ball_postprocess / 1000) if avg_ball_postprocess > 0 else 0
        fps_loss_preprocess_pose = 1 / (avg_preprocess_pose / 1000) if avg_preprocess_pose > 0 else 0
        fps_loss_pose_inference = 1 / (avg_pose_inference / 1000) if avg_pose_inference > 0 else 0
        fps_loss_pose_postprocess = 1 / (avg_pose_postprocess / 1000) if avg_pose_postprocess > 0 else 0
        fps_loss_tracking = 1 / (avg_tracking / 1000) if avg_tracking > 0 else 0
        fps_loss_conversion = 1 / (avg_conversion / 1000) if avg_conversion > 0 else 0
        
        print(f"\n✅ Full Pipeline Profiling Complete!")
        print(f"   Device: {device}")
        print(f"\n   📊 DETAILED BREAKDOWN:")
        print(f"   ─────────────────────────────────────────────────────────")
        print(f"   Shared Preprocessing:")
        print(f"     • Preprocessing (once): {avg_preprocess_ball:6.2f} ms  ({avg_preprocess_ball/avg_time_per_frame*100:5.1f}%)")
        print(f"   ─────────────────────────────────────────────────────────")
        print(f"   Ball Detection Pipeline:")
        print(f"     • Inference:            {avg_ball_inference:6.2f} ms  ({avg_ball_inference/avg_time_per_frame*100:5.1f}%)")
        print(f"     • Postprocessing (NMS): {avg_ball_postprocess:6.2f} ms  ({avg_ball_postprocess/avg_time_per_frame*100:5.1f}%)")
        print(f"   ─────────────────────────────────────────────────────────")
        print(f"   Pose Detection Pipeline:")
        print(f"     • Inference:            {avg_pose_inference:6.2f} ms  ({avg_pose_inference/avg_time_per_frame*100:5.1f}%)")
        print(f"     • Postprocessing:       {avg_pose_postprocess:6.2f} ms  ({avg_pose_postprocess/avg_time_per_frame*100:5.1f}%)")
        print(f"   ─────────────────────────────────────────────────────────")
        print(f"   Tracking & Conversion:")
        print(f"     • Tracking (NN):        {avg_tracking:6.2f} ms  ({avg_tracking/avg_time_per_frame*100:5.1f}%)")
        print(f"     • Format Conversion:    {avg_conversion:6.2f} ms  ({avg_conversion/avg_time_per_frame*100:5.1f}%)")
        print(f"   ─────────────────────────────────────────────────────────")
        print(f"   TOTAL per frame:          {avg_time_per_frame * 1000:6.2f} ms")
        print(f"   Estimated Pipeline FPS:   {pipeline_fps:6.2f} FPS")
        print(f"\n   ⚠️  FPS LOSS ANALYSIS:")
        print(f"   ─────────────────────────────────────────────────────────")
        
        # Calculate cumulative FPS loss
        inference_only_time = (time_ball_inference + time_pose_inference) / num_iterations
        inference_only_fps = 1 / inference_only_time
        overhead_time = avg_time_per_frame - inference_only_time
        fps_loss = inference_only_fps - pipeline_fps
        
        print(f"   Inference only (both models): {inference_only_fps:.2f} FPS")
        print(f"   Full pipeline:                {pipeline_fps:.2f} FPS")
        print(f"   FPS lost to overhead:         {fps_loss:.2f} FPS ({fps_loss/inference_only_fps*100:.1f}%)")
        print(f"   Overhead time per frame:      {overhead_time * 1000:.2f} ms")
        
    except Exception as e:
        print(f"\n❌ Error occurred: {e}")
        import traceback
        traceback.print_exc()
    
    print("-" * 60 + "\n")


def main():
    """Main benchmark execution"""
    print("=" * 60)
    print("OpenVINO Model Benchmark")
    print("Matches C++ Engine Implementation")
    print("=" * 60)
    print()
    
    # Check OpenVINO version
    core = Core()
    print(f"OpenVINO version: {core.get_versions('CPU')['CPU'].description}")
    print(f"Available devices: {core.available_devices}")
    print()
    
    # Determine which device to use
    devices_to_test = []
    if "GPU" in core.available_devices:
        devices_to_test.append("GPU")
        print("✓ GPU device found - will benchmark on GPU")
    else:
        print("⚠️  No GPU device found - will benchmark on CPU only")
    
    if "CPU" in core.available_devices:
        devices_to_test.append("CPU")
    
    if not devices_to_test:
        print("❌ No devices available!")
        return
    
    print()
    
    # Benchmark each device
    for device in devices_to_test:
        print(f"\n{'='*60}")
        print(f"Testing on device: {device}")
        print(f"{'='*60}\n")
        
        # Benchmark ball detection model
        benchmark_openvino_model(
            model_xml_path=BALL_MODEL_XML,
            model_name="Ball Detection Model (V8_balls_held)",
            image_path=TEST_IMAGE_PATH,
            device=device,
            num_iterations=100
        )
        
        # Benchmark pose detection model
        benchmark_openvino_model(
            model_xml_path=POSE_MODEL_XML,
            model_name="Pose Detection Model (yoloposenano)",
            image_path=TEST_IMAGE_PATH,
            device=device,
            num_iterations=100
        )
        
        # Benchmark combined models (ball + pose together)
        benchmark_combined_models(
            ball_model_xml_path=BALL_MODEL_XML,
            pose_model_xml_path=POSE_MODEL_XML,
            image_path=TEST_IMAGE_PATH,
            device=device,
            num_iterations=100
        )
        
        # Benchmark full pipeline with detailed profiling
        benchmark_full_pipeline(
            ball_model_xml_path=BALL_MODEL_XML,
            pose_model_xml_path=POSE_MODEL_XML,
            image_path=TEST_IMAGE_PATH,
            device=device,
            num_iterations=100
        )
    
    print("\n" + "=" * 60)
    print("BENCHMARK SUMMARY")
    print("=" * 60)
    print("This script uses OpenVINO Python API, which directly")
    print("matches how the C++ engine loads and runs models:")
    print("  1. Core.read_model() - Load model from .xml/.bin")
    print("  2. Core.compile_model() - Compile for target device")
    print("  3. create_infer_request() - Create inference request")
    print("  4. infer_request.infer() - Run inference")
    print()
    print("The C++ engine uses the same OpenVINO API calls.")
    print("=" * 60)


if __name__ == "__main__":
    main()