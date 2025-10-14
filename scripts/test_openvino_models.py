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