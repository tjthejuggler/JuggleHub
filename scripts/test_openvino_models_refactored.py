#!/usr/bin/env python3
"""
test_openvino_models_refactored.py
Purpose: Benchmark OpenVINO models using advanced optimization techniques
This version implements:
1. Asynchronous Inference API for overlapping GPU/CPU work
2. Preprocessing API to offload work to the accelerator
3. Performance hints for throughput optimization
4. Corrected benchmark loop (no disk I/O in hot path)

Based on test_openvino_models.py with significant performance optimizations.
"""

import time
import numpy as np
import cv2
from pathlib import Path
from openvino.runtime import Core, Type
from openvino.preprocess import PrePostProcessor, ResizeAlgorithm
import openvino.properties.hint as ov_hint

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
    NOTE: This function is kept for compatibility with original benchmarks,
    but the optimized pipeline uses the Preprocessing API instead.
    
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


def benchmark_optimized_async_pipeline(
    ball_model_xml_path: str,
    pose_model_xml_path: str,
    image_path: str,
    device: str = "GPU",
    num_iterations: int = 100
):
    """
    Benchmark the full pipeline using OpenVINO's best practices:
    1. Asynchronous Inference API to overlap execution
    2. Preprocessing API to offload work to the accelerator
    3. Performance hints for throughput optimization
    4. Corrected benchmark loop (no disk I/O in hot path)
    
    This represents the MAXIMUM achievable performance with OpenVINO.
    
    Args:
        ball_model_xml_path: Path to ball detection .xml model file
        pose_model_xml_path: Path to pose detection .xml model file
        image_path: Path to test image
        device: OpenVINO device ('CPU', 'GPU', 'AUTO')
        num_iterations: Number of iterations for benchmarking
    """
    print("-" * 60)
    print(f"🚀 Benchmarking: OPTIMIZED Async Pipeline")
    print(f"   Ball Model: {Path(ball_model_xml_path).name}")
    print(f"   Pose Model: {Path(pose_model_xml_path).name}")
    print(f"   Device: {device}")
    print(f"\n   Optimizations Applied:")
    print(f"   ✓ Asynchronous Inference API")
    print(f"   ✓ Preprocessing API (GPU-accelerated)")
    print(f"   ✓ THROUGHPUT performance hint")
    print(f"   ✓ Corrected benchmark (no disk I/O)")
    
    try:
        # --- Initialization ---
        print("\n   [1/6] Initializing OpenVINO Core...")
        core = Core()
        
        # Check device availability
        available_devices = core.available_devices
        print(f"   Available devices: {available_devices}")
        if device not in available_devices and device != "AUTO":
            print(f"   ⚠️  Warning: {device} not available, falling back to CPU")
            device = "CPU"
        
        # Load the original image ONCE, in its raw format
        print("   [2/6] Loading test image (once)...")
        img_raw = cv2.imread(image_path)
        if img_raw is None:
            raise ValueError(f"Could not read image: {image_path}")
        
        print(f"   Image shape: {img_raw.shape} (HWC, uint8)")

        # --- Model Loading and Preprocessing Configuration ---
        print("   [3/6] Reading models and applying Preprocessing API...")
        
        # Ball Model with Preprocessing API
        ball_model = core.read_model(model=ball_model_xml_path)
        ppp_ball = PrePostProcessor(ball_model)
        ppp_ball.input().tensor() \
            .set_element_type(Type.u8) \
            .set_layout('NHWC') \
            .set_spatial_static_shape(img_raw.shape[0], img_raw.shape[1])
        ppp_ball.input().preprocess() \
            .resize(ResizeAlgorithm.RESIZE_LINEAR) \
            .convert_layout('NCHW') \
            .convert_element_type(Type.f32) \
            .scale(255.0)  # Normalizes the data
        ball_model = ppp_ball.build()
        print(f"   ✓ Ball model preprocessing configured")

        # Pose Model with Preprocessing API (assuming same preprocessing)
        pose_model = core.read_model(model=pose_model_xml_path)
        ppp_pose = PrePostProcessor(pose_model)
        ppp_pose.input().tensor() \
            .set_element_type(Type.u8) \
            .set_layout('NHWC') \
            .set_spatial_static_shape(img_raw.shape[0], img_raw.shape[1])
        ppp_pose.input().preprocess() \
            .resize(ResizeAlgorithm.RESIZE_LINEAR) \
            .convert_layout('NCHW') \
            .convert_element_type(Type.f32) \
            .scale(255.0)
        pose_model = ppp_pose.build()
        print(f"   ✓ Pose model preprocessing configured")

        # --- Model Compilation with Performance Hints ---
        print("   [4/6] Compiling models with THROUGHPUT hint...")
        # Configuration for maximizing throughput
        compile_config = {"PERFORMANCE_HINT": "THROUGHPUT"}
        
        ball_compiled = core.compile_model(model=ball_model, device_name=device, config=compile_config)
        pose_compiled = core.compile_model(model=pose_model, device_name=device, config=compile_config)
        print(f"   ✓ Models compiled for {device}")
        
        # --- Create Async Inference Requests ---
        print("   [5/6] Creating asynchronous inference requests...")
        ball_infer = ball_compiled.create_infer_request()
        pose_infer = pose_compiled.create_infer_request()
        
        # --- Prepare input data (once) ---
        # Note: Preprocessing API expects NHWC uint8 data, so we just add a batch dim.
        input_data = np.expand_dims(img_raw, axis=0)
        print(f"   Input data shape: {input_data.shape} (NHWC, uint8)")

        # Warm-up runs
        print("   Performing warm-up runs...")
        ball_infer.start_async({0: input_data})
        pose_infer.start_async({0: input_data})
        ball_infer.wait()
        pose_infer.wait()

        # --- Benchmarking Loop ---
        print(f"   [6/6] Running optimized async benchmark ({num_iterations} iterations)...")
        start_time = time.perf_counter()
        
        for _ in range(num_iterations):
            # 1. Start both inferences asynchronously (non-blocking)
            #    This allows the GPU to pipeline both model executions
            ball_infer.start_async({0: input_data})
            pose_infer.start_async({0: input_data})
            
            # 2. Wait for the ball model to finish
            ball_infer.wait()
            
            # 3. While the pose model may still be running on GPU,
            #    do CPU-bound post-processing for the ball model
            output_tensor = ball_infer.get_output_tensor()
            output_data = output_tensor.data
            
            # Simulate ball post-processing (parsing YOLO output)
            num_channels = 4 + 2  # 4 bbox coords + 2 classes
            output_buffer = np.array(output_data).reshape(num_channels, -1).T
            
            # Parse detections (simplified)
            boxes = []
            confidences = []
            for i in range(min(100, output_buffer.shape[0])):
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
                _ = cv2.dnn.NMSBoxes(boxes_array.tolist(), confidences_array.tolist(), 0.25, 0.45)
            
            # 4. Now wait for the pose model to finish
            #    It may have already finished, in which case wait() returns immediately
            pose_infer.wait()
            
            # 5. Do CPU-bound post-processing for the pose model
            output_tensor = pose_infer.get_output_tensor()
            output_data = output_tensor.data
            
            # Simulate pose post-processing (parsing keypoints)
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
            
            # 6. Simulate tracking
            if len(boxes) > 0:
                for box in boxes[:5]:
                    distances = [np.sqrt((box[0] - b[0])**2 + (box[1] - b[1])**2) for b in boxes]
                    min_dist = min(distances) if distances else 0

        end_time = time.perf_counter()

        # --- Results ---
        total_time = end_time - start_time
        avg_time_per_frame = total_time / num_iterations
        pipeline_fps = 1 / avg_time_per_frame

        print(f"\n✅ Optimized Async Pipeline Benchmark Complete!")
        print(f"   Device: {device}")
        print(f"   Total time: {total_time:.2f} s")
        print(f"   Average time per frame: {avg_time_per_frame * 1000:.2f} ms")
        print(f"   Estimated Pipeline FPS:   {pipeline_fps:.2f} FPS")
        print(f"\n   💡 Performance Gains:")
        print(f"   • Async API: Overlaps GPU execution of both models")
        print(f"   • Preprocessing API: Offloads resize/normalize to GPU")
        print(f"   • Throughput hint: Optimizes device for max FPS")
        print(f"   • No disk I/O: Accurate real-world performance")

    except Exception as e:
        print(f"\n❌ Error occurred: {e}")
        import traceback
        traceback.print_exc()
    
    print("-" * 60 + "\n")


def benchmark_optimized_preprocessing_only(
    ball_model_xml_path: str,
    pose_model_xml_path: str,
    image_path: str,
    device: str = "GPU",
    num_iterations: int = 100
):
    """
    Benchmark with Preprocessing API only (synchronous inference).
    This shows the benefit of GPU-accelerated preprocessing alone.
    
    Args:
        ball_model_xml_path: Path to ball detection .xml model file
        pose_model_xml_path: Path to pose detection .xml model file
        image_path: Path to test image
        device: OpenVINO device ('CPU', 'GPU', 'AUTO')
        num_iterations: Number of iterations for benchmarking
    """
    print("-" * 60)
    print(f"🚀 Benchmarking: Preprocessing API Only (Sync)")
    print(f"   Device: {device}")
    print(f"\n   Optimizations Applied:")
    print(f"   ✓ Preprocessing API (GPU-accelerated)")
    print(f"   ✓ Corrected benchmark (no disk I/O)")
    
    try:
        # Initialize OpenVINO Core
        print("\n   [1/5] Initializing OpenVINO Core...")
        core = Core()
        
        # Check device availability
        available_devices = core.available_devices
        if device not in available_devices and device != "AUTO":
            print(f"   ⚠️  Warning: {device} not available, falling back to CPU")
            device = "CPU"
        
        # Load the original image ONCE
        print("   [2/5] Loading test image (once)...")
        img_raw = cv2.imread(image_path)
        if img_raw is None:
            raise ValueError(f"Could not read image: {image_path}")

        # Configure models with Preprocessing API
        print("   [3/5] Reading models and applying Preprocessing API...")
        
        ball_model = core.read_model(model=ball_model_xml_path)
        ppp_ball = PrePostProcessor(ball_model)
        ppp_ball.input().tensor() \
            .set_element_type(Type.u8) \
            .set_layout('NHWC') \
            .set_spatial_static_shape(img_raw.shape[0], img_raw.shape[1])
        ppp_ball.input().preprocess() \
            .resize(ResizeAlgorithm.RESIZE_LINEAR) \
            .convert_layout('NCHW') \
            .convert_element_type(Type.f32) \
            .scale(255.0)
        ball_model = ppp_ball.build()

        pose_model = core.read_model(model=pose_model_xml_path)
        ppp_pose = PrePostProcessor(pose_model)
        ppp_pose.input().tensor() \
            .set_element_type(Type.u8) \
            .set_layout('NHWC') \
            .set_spatial_static_shape(img_raw.shape[0], img_raw.shape[1])
        ppp_pose.input().preprocess() \
            .resize(ResizeAlgorithm.RESIZE_LINEAR) \
            .convert_layout('NCHW') \
            .convert_element_type(Type.f32) \
            .scale(255.0)
        pose_model = ppp_pose.build()

        # Compile models (without throughput hint for comparison)
        print("   [4/5] Compiling models...")
        ball_compiled = core.compile_model(model=ball_model, device_name=device)
        pose_compiled = core.compile_model(model=pose_model, device_name=device)
        
        # Create inference requests
        ball_infer = ball_compiled.create_infer_request()
        pose_infer = pose_compiled.create_infer_request()
        
        # Prepare input data
        input_data = np.expand_dims(img_raw, axis=0)

        # Warm-up runs
        print("   Performing warm-up runs...")
        ball_infer.infer({0: input_data})
        pose_infer.infer({0: input_data})

        # Benchmark
        print(f"   [5/5] Running benchmark ({num_iterations} iterations)...")
        start_time = time.perf_counter()
        
        for _ in range(num_iterations):
            ball_infer.infer({0: input_data})
            pose_infer.infer({0: input_data})
            
        end_time = time.perf_counter()

        # Results
        total_time = end_time - start_time
        avg_time_per_frame = total_time / num_iterations
        pipeline_fps = 1 / avg_time_per_frame

        print(f"\n✅ Preprocessing API Benchmark Complete!")
        print(f"   Device: {device}")
        print(f"   Total time: {total_time:.2f} s")
        print(f"   Average time per frame: {avg_time_per_frame * 1000:.2f} ms")
        print(f"   Estimated Pipeline FPS:   {pipeline_fps:.2f} FPS")

    except Exception as e:
        print(f"\n❌ Error occurred: {e}")
        import traceback
        traceback.print_exc()
    
    print("-" * 60 + "\n")


def benchmark_original_for_comparison(
    ball_model_xml_path: str,
    pose_model_xml_path: str,
    image_path: str,
    device: str = "GPU",
    num_iterations: int = 100
):
    """
    Benchmark using the original approach (for comparison).
    This uses CPU preprocessing and synchronous inference.
    
    Args:
        ball_model_xml_path: Path to ball detection .xml model file
        pose_model_xml_path: Path to pose detection .xml model file
        image_path: Path to test image
        device: OpenVINO device ('CPU', 'GPU', 'AUTO')
        num_iterations: Number of iterations for benchmarking
    """
    print("-" * 60)
    print(f"🚀 Benchmarking: Original Approach (Baseline)")
    print(f"   Device: {device}")
    print(f"\n   Approach:")
    print(f"   • CPU-based preprocessing")
    print(f"   • Synchronous inference")
    print(f"   • Corrected benchmark (no disk I/O)")
    
    try:
        # Initialize OpenVINO Core
        print("\n   [1/5] Initializing OpenVINO Core...")
        core = Core()
        
        # Check device availability
        available_devices = core.available_devices
        if device not in available_devices and device != "AUTO":
            print(f"   ⚠️  Warning: {device} not available, falling back to CPU")
            device = "CPU"
        
        # Load models
        print("   [2/5] Reading models...")
        ball_model = core.read_model(model=ball_model_xml_path)
        pose_model = core.read_model(model=pose_model_xml_path)
        
        # Compile models
        print("   [3/5] Compiling models...")
        ball_compiled = core.compile_model(model=ball_model, device_name=device)
        pose_compiled = core.compile_model(model=pose_model, device_name=device)
        
        # Create inference requests
        print("   [4/5] Creating inference requests...")
        ball_infer = ball_compiled.create_infer_request()
        pose_infer = pose_compiled.create_infer_request()
        
        ball_input = ball_compiled.input(0)
        pose_input = pose_compiled.input(0)
        
        # Preprocess image ONCE (corrected benchmark)
        print("   Preprocessing image (once)...")
        input_data = preprocess_image(image_path)

        # Warm-up runs
        print("   Performing warm-up runs...")
        ball_infer.infer({ball_input: input_data})
        pose_infer.infer({pose_input: input_data})

        # Benchmark
        print(f"   [5/5] Running benchmark ({num_iterations} iterations)...")
        start_time = time.perf_counter()
        
        for _ in range(num_iterations):
            ball_infer.infer({ball_input: input_data})
            pose_infer.infer({pose_input: input_data})
            
        end_time = time.perf_counter()

        # Results
        total_time = end_time - start_time
        avg_time_per_frame = total_time / num_iterations
        pipeline_fps = 1 / avg_time_per_frame

        print(f"\n✅ Original Approach Benchmark Complete!")
        print(f"   Device: {device}")
        print(f"   Total time: {total_time:.2f} s")
        print(f"   Average time per frame: {avg_time_per_frame * 1000:.2f} ms")
        print(f"   Estimated Pipeline FPS:   {pipeline_fps:.2f} FPS")

    except Exception as e:
        print(f"\n❌ Error occurred: {e}")
        import traceback
        traceback.print_exc()
    
    print("-" * 60 + "\n")


def main():
    """Main benchmark execution"""
    print("=" * 60)
    print("OpenVINO Model Benchmark - REFACTORED & OPTIMIZED")
    print("=" * 60)
    print()
    print("This script demonstrates the performance impact of:")
    print("1. Asynchronous Inference API")
    print("2. Preprocessing API (GPU-accelerated)")
    print("3. Performance Hints (THROUGHPUT mode)")
    print("4. Corrected benchmarking (no disk I/O in hot path)")
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
        
        # 1. Original approach (baseline)
        benchmark_original_for_comparison(
            ball_model_xml_path=BALL_MODEL_XML,
            pose_model_xml_path=POSE_MODEL_XML,
            image_path=TEST_IMAGE_PATH,
            device=device,
            num_iterations=100
        )
        
        # 2. Preprocessing API only
        benchmark_optimized_preprocessing_only(
            ball_model_xml_path=BALL_MODEL_XML,
            pose_model_xml_path=POSE_MODEL_XML,
            image_path=TEST_IMAGE_PATH,
            device=device,
            num_iterations=100
        )
        
        # 3. Full optimization (async + preprocessing + throughput hint)
        benchmark_optimized_async_pipeline(
            ball_model_xml_path=BALL_MODEL_XML,
            pose_model_xml_path=POSE_MODEL_XML,
            image_path=TEST_IMAGE_PATH,
            device=device,
            num_iterations=100
        )
    
    print("\n" + "=" * 60)
    print("BENCHMARK SUMMARY")
    print("=" * 60)
    print("This refactored script demonstrates three approaches:")
    print()
    print("1. ORIGINAL (Baseline):")
    print("   • CPU preprocessing (resize, normalize, transpose)")
    print("   • Synchronous inference (blocking)")
    print("   • Default device configuration")
    print()
    print("2. PREPROCESSING API:")
    print("   • GPU-accelerated preprocessing")
    print("   • Synchronous inference")
    print("   • Reduces CPU load and data transfer")
    print()
    print("3. FULLY OPTIMIZED (Async + Preprocessing + Hints):")
    print("   • GPU-accelerated preprocessing")
    print("   • Asynchronous inference (overlapping execution)")
    print("   • THROUGHPUT performance hint")
    print("   • Maximum achievable performance")
    print()
    print("Expected Performance Gains:")
    print("   • Preprocessing API: 5-15% FPS boost")
    print("   • Async API: 30-100%+ FPS boost")
    print("   • Throughput Hint: 5-10% FPS boost")
    print("   • Combined: Potentially 50-150%+ total improvement")
    print("=" * 60)


if __name__ == "__main__":
    main()