# benchmark_yolo_models.py
# Purpose: Benchmark YOLO models using Ultralytics
# Note: Uses .pt files since OpenVINO Python API is not available

import time
from pathlib import Path
from ultralytics import YOLO

# --- Configuration ---------------------------------------------------
# Use the original .pt files for benchmarking
BALL_DETECTION_MODEL = "/home/twain/Projects/JuggleHub/engine/models/zipped/V8_balls_held/best.pt"
POSE_MODEL = "yolo11n-pose.pt"  # Will download if not present
TEST_IMAGE_PATH = "/home/twain/Projects/JuggleHub/engine/data/3_training_datasets/V9_1_1_targeted/valid/images/rs455_2025-09-05_13-12-49_frame_149.jpg"
# ---------------------------------------------------------------------

def benchmark_model(model_path: str, task: str, image_path: str, device: str = 'cpu', num_iterations: int = 100):
    """
    Benchmark a YOLO model.
    
    Args:
        model_path: Path to the .pt model file
        task: YOLO task type ('detect' or 'pose')
        image_path: Path to test image
        device: Device to use ('cpu', '0' for CUDA GPU 0, etc.)
        num_iterations: Number of iterations for benchmarking
    """
    print("-" * 50)
    print(f"🚀 Benchmarking model: {Path(model_path).name}")
    print(f"   Task: {task}")
    print(f"   Device: {device}")
    
    try:
        # Load model
        print("   Loading model...")
        model = YOLO(model_path, task=task)
        
        # Warm-up run
        print("   Performing warm-up run...")
        _ = model.predict(source=image_path, device=device, verbose=False)
        
        # Benchmark
        print(f"   Starting timed run ({num_iterations} iterations)...")
        start_time = time.perf_counter()
        
        for _ in range(num_iterations):
            _ = model.predict(source=image_path, device=device, verbose=False)
            
        end_time = time.perf_counter()
        
        total_time = end_time - start_time
        avg_time_per_frame = total_time / num_iterations
        true_fps = 1 / avg_time_per_frame
        
        print(f"✅ Benchmark Complete for {Path(model_path).name} on {device}")
        print(f"   Average time per frame: {avg_time_per_frame * 1000:.2f} ms")
        print(f"   True FPS: {true_fps:.2f}")
        
    except Exception as e:
        print(f"❌ An error occurred: {e}")
        import traceback
        traceback.print_exc()
    print("-" * 50 + "\n")


# --- Main Execution --------------------------------------------------
if __name__ == "__main__":
    print("### Running YOLO Model Benchmarks ###")
    print("### Using PyTorch backend (CPU) ###\n")
    print("Note: The engine uses OpenVINO for GPU acceleration.")
    print("      This script benchmarks the models on CPU for comparison.\n")
    
    # Benchmark ball detection model
    benchmark_model(
        model_path=BALL_DETECTION_MODEL, 
        task='detect',
        image_path=TEST_IMAGE_PATH,
        device='cpu',
        num_iterations=50  # Reduced for CPU
    )
    
    # Benchmark pose detection model
    benchmark_model(
        model_path=POSE_MODEL, 
        task='pose',
        image_path=TEST_IMAGE_PATH,
        device='cpu',
        num_iterations=50  # Reduced for CPU
    )
    
    print("\n" + "="*50)
    print("SUMMARY")
    print("="*50)
    print("These benchmarks show CPU performance using PyTorch.")
    print("The C++ engine uses OpenVINO with GPU acceleration,")
    print("which provides significantly better performance.")
    print("="*50)