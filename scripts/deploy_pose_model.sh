#!/bin/bash

# deploy_pose_model.sh - Automate the deployment of YOLO pose models
# Usage: ./deploy__pose_model.sh <model_name> [options]

set -e  # Exit on any error

# Default values
DEFAULT_PT_PATH="$HOME/Downloads/best.pt"
DEFAULT_MODEL_SIZE="nano"
DEFAULT_IMGSZ=640
DEPLOY_FLAG=false

# Project paths
PROJECT_ROOT="/home/twain/Projects/JuggleHub"
MODELS_DIR="$PROJECT_ROOT/engine/models"
ZIPPED_DIR="$MODELS_DIR/zipped"

# Function to display usage
usage() {
    echo "Usage: $0 <model_name> [OPTIONS]"
    echo ""
    echo "Required arguments:"
    echo "  model_name          Name of the model (e.g., V2_3_lonely_hands)"
    echo ""
    echo "Optional arguments:"
    echo "  -p, --pt-path PATH  Path to the best.pt file (default: ~/Downloads/best.pt)"
    echo "  -s, --size SIZE     Model size: 'nano' or 'small' (default: nano)"
    echo "  -i, --imgsz SIZE    Image size for export (default: 640)"
    echo "  -d, --deploy        Deploy to root models directory (replaces current models)"
    echo "  -h, --help          Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 V2_3_lonely_hands"
    echo "  $0 V2_3_lonely_hands -p /path/to/model.pt -s small"
    echo "  $0 V2_3_lonely_hands -i 1280"
    echo "  $0 V2_3_lonely_hands --deploy"
    exit 1
}

# Function to log messages
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1"
}

# Function to validate model size
validate_model_size() {
    if [[ "$1" != "nano" && "$1" != "small" ]]; then
        echo "Error: Model size must be 'nano' or 'small', got: $1"
        exit 1
    fi
}

# Function to check if file exists
check_file_exists() {
    if [[ ! -f "$1" ]]; then
        echo "Error: File not found: $1"
        exit 1
    fi
}

# Function to create directory if it doesn't exist
ensure_directory() {
    if [[ ! -d "$1" ]]; then
        log "Creating directory: $1"
        mkdir -p "$1"
    fi
}

# Parse command line arguments
if [[ $# -eq 0 ]]; then
    echo "Error: Model name is required"
    usage
fi

MODEL_NAME="$1"
shift

PT_PATH="$DEFAULT_PT_PATH"
MODEL_SIZE="$DEFAULT_MODEL_SIZE"
IMGSZ="$DEFAULT_IMGSZ"

while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--pt-path)
            PT_PATH="$2"
            shift 2
            ;;
        -s|--size)
            MODEL_SIZE="$2"
            shift 2
            ;;
        -i|--imgsz)
            IMGSZ="$2"
            shift 2
            ;;
        -d|--deploy)
            DEPLOY_FLAG=true
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Error: Unknown option $1"
            usage
            ;;
    esac
done

# Validate inputs
validate_model_size "$MODEL_SIZE"
check_file_exists "$PT_PATH"

# Set up paths based on model size
if [[ "$MODEL_SIZE" == "nano" ]]; then
    TARGET_XML="yolo11n-pose.xml"
    TARGET_BIN="yolo11n-pose.bin"
else
    TARGET_XML="yolo11s-pose.xml"
    TARGET_BIN="yolo11s-pose.bin"
fi

MODEL_ZIPPED_DIR="$ZIPPED_DIR/$MODEL_NAME"
MODEL_DEPLOY_DIR="$MODELS_DIR/$MODEL_NAME"

log "Starting deployment for model: $MODEL_NAME"
log "Model size: $MODEL_SIZE"
log "Image size: $IMGSZ"
log "Source PT file: $PT_PATH"
log "Deploy flag: $DEPLOY_FLAG"

# Step 1: Create zipped directory and copy PT file
log "Step 1: Setting up zipped directory"
ensure_directory "$MODEL_ZIPPED_DIR"

TARGET_PT_PATH="$MODEL_ZIPPED_DIR/best.pt"
if [[ "$PT_PATH" != "$TARGET_PT_PATH" ]]; then
    log "Copying $PT_PATH to $TARGET_PT_PATH"
    cp "$PT_PATH" "$TARGET_PT_PATH"
else
    log "PT file already in correct location"
fi

# Step 2: Export model to OpenVINO format
log "Step 2: Exporting model to OpenVINO format"
cd "$MODEL_ZIPPED_DIR"

log "Running YOLO export command..."
yolo export model="$TARGET_PT_PATH" format=openvino imgsz="$IMGSZ"

# Check if export was successful
OPENVINO_DIR="$MODEL_ZIPPED_DIR/best_openvino_model"
if [[ ! -d "$OPENVINO_DIR" ]]; then
    echo "Error: OpenVINO export failed - directory not found: $OPENVINO_DIR"
    exit 1
fi

check_file_exists "$OPENVINO_DIR/best.xml"
check_file_exists "$OPENVINO_DIR/best.bin"

# Step 3: Create model deployment directory and copy/rename files
log "Step 3: Setting up model deployment directory"
ensure_directory "$MODEL_DEPLOY_DIR"

log "Copying and renaming OpenVINO files"
cp "$OPENVINO_DIR/best.xml" "$MODEL_DEPLOY_DIR/$TARGET_XML"
cp "$OPENVINO_DIR/best.bin" "$MODEL_DEPLOY_DIR/$TARGET_BIN"

log "Model files deployed to: $MODEL_DEPLOY_DIR"
log "  - $TARGET_XML"
log "  - $TARGET_BIN"

# Step 4: Deploy to root models directory if requested
if [[ "$DEPLOY_FLAG" == true ]]; then
    log "Step 4: Deploying to root models directory"
    
    # Backup existing files
    ROOT_XML="$MODELS_DIR/$TARGET_XML"
    ROOT_BIN="$MODELS_DIR/$TARGET_BIN"
    
    if [[ -f "$ROOT_XML" ]]; then
        BACKUP_XML="$ROOT_XML.backup.$(date +%Y%m%d_%H%M%S)"
        log "Backing up existing $TARGET_XML to $(basename "$BACKUP_XML")"
        cp "$ROOT_XML" "$BACKUP_XML"
    fi
    
    if [[ -f "$ROOT_BIN" ]]; then
        BACKUP_BIN="$ROOT_BIN.backup.$(date +%Y%m%d_%H%M%S)"
        log "Backing up existing $TARGET_BIN to $(basename "$BACKUP_BIN")"
        cp "$ROOT_BIN" "$BACKUP_BIN"
    fi
    
    # Deploy new files
    log "Deploying new model files to root directory"
    cp "$MODEL_DEPLOY_DIR/$TARGET_XML" "$ROOT_XML"
    cp "$MODEL_DEPLOY_DIR/$TARGET_BIN" "$ROOT_BIN"
    
    log "Root model files updated:"
    log "  - $TARGET_XML"
    log "  - $TARGET_BIN"
fi

log "Deployment completed successfully!"
log "Model '$MODEL_NAME' is ready for use."

# Summary
echo ""
echo "=== Deployment Summary ==="
echo "Model Name: $MODEL_NAME"
echo "Model Size: $MODEL_SIZE"
echo "Image Size: $IMGSZ"
echo "Source PT: $PT_PATH"
echo "Deployed to: $MODEL_DEPLOY_DIR"
if [[ "$DEPLOY_FLAG" == true ]]; then
    echo "Root deployment: YES"
else
    echo "Root deployment: NO (use --deploy flag to deploy to root)"
fi
echo "=========================="