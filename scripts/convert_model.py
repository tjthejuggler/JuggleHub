import openvino as ov
import sys
from ultralytics import YOLO

def convert_model(model_path, output_dir):
    try:
        print(f"Loading YOLO model from {model_path}...")
        model = YOLO(model_path)
        
        print(f"Exporting model to ONNX format...")
        # The export function returns the path to the exported model
        onnx_path = model.export(format="onnx")
        
        print(f"Loading ONNX model from {onnx_path}...")
        ov_model = ov.convert_model(onnx_path)
        
        output_path = f"{output_dir}/{model_path.split('/')[-1].replace('.pt', '.xml')}"
        
        print(f"Serializing model to {output_path}...")
        ov.save_model(ov_model, output_path)
        
        print("Conversion successful!")
    except Exception as e:
        print(f"An error occurred during model conversion: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python convert_model.py <path_to_model> <output_directory>")
        sys.exit(1)
    
    model_path = sys.argv[1]
    output_dir = sys.argv[2]
    
    convert_model(model_path, output_dir)