#!/usr/bin/env python3
"""
Dataset Preparation Script for YOLOv8 Training

This script takes annotated images from the 2_tagged_and_annotated/ directory
and splits them into training and validation sets using the 80/20 rule.

Usage:
    python3 scripts/prepare_dataset.py --dataset-name V1_generalist --tags tag1 tag2 tag3
    python3 scripts/prepare_dataset.py --dataset-name V2_specialized --tags high_quality_lighting good_contrast
    python3 scripts/prepare_dataset.py --dataset-name test_dataset --tags all
"""

import os
import sys
import shutil
import random
import argparse
from pathlib import Path
from typing import List, Tuple, Set
import yaml

def find_image_annotation_pairs(source_dir: Path, tags: List[str]) -> List[Tuple[Path, Path]]:
    """
    Find all image/annotation pairs in the specified tagged directories.
    
    Args:
        source_dir: Path to 2_tagged_and_annotated directory
        tags: List of tag directories to include, or ['all'] for all directories
        
    Returns:
        List of (image_path, annotation_path) tuples
    """
    pairs = []
    
    # Get list of directories to process
    if tags == ['all']:
        tag_dirs = [d for d in source_dir.iterdir() if d.is_dir()]
    else:
        tag_dirs = [source_dir / tag for tag in tags if (source_dir / tag).exists()]
        
        # Warn about missing tag directories
        for tag in tags:
            if not (source_dir / tag).exists():
                print(f"Warning: Tag directory '{tag}' not found in {source_dir}")
    
    print(f"Processing {len(tag_dirs)} tag directories:")
    for tag_dir in tag_dirs:
        print(f"  - {tag_dir.name}")
    
    # Supported image extensions
    image_extensions = {'.jpg', '.jpeg', '.png', '.bmp', '.tiff', '.tif'}
    
    # Find all image/annotation pairs
    for tag_dir in tag_dirs:
        if not tag_dir.is_dir():
            continue
            
        print(f"\nScanning {tag_dir.name}...")
        tag_pairs = 0
        
        for image_file in tag_dir.iterdir():
            if image_file.suffix.lower() not in image_extensions:
                continue
                
            # Look for corresponding annotation file (.txt)
            annotation_file = image_file.with_suffix('.txt')
            
            if annotation_file.exists():
                pairs.append((image_file, annotation_file))
                tag_pairs += 1
            else:
                print(f"  Warning: No annotation found for {image_file.name}")
        
        print(f"  Found {tag_pairs} image/annotation pairs")
    
    return pairs

def create_dataset_structure(output_dir: Path) -> Tuple[Path, Path]:
    """
    Create the YOLOv8 dataset directory structure.
    
    Args:
        output_dir: Base directory for the dataset
        
    Returns:
        Tuple of (train_dir, valid_dir)
    """
    # Create main directories
    train_dir = output_dir / "train"
    valid_dir = output_dir / "valid"
    
    # Create subdirectories for images and labels
    for split_dir in [train_dir, valid_dir]:
        (split_dir / "images").mkdir(parents=True, exist_ok=True)
        (split_dir / "labels").mkdir(parents=True, exist_ok=True)
    
    return train_dir, valid_dir

def copy_files(pairs: List[Tuple[Path, Path]], train_dir: Path, valid_dir: Path, 
               train_split: float = 0.8) -> Tuple[int, int]:
    """
    Copy image/annotation pairs to train and validation directories.
    
    Args:
        pairs: List of (image_path, annotation_path) tuples
        train_dir: Training directory
        valid_dir: Validation directory
        train_split: Fraction of data to use for training (default: 0.8)
        
    Returns:
        Tuple of (train_count, valid_count)
    """
    # Shuffle the pairs to ensure random distribution
    shuffled_pairs = pairs.copy()
    random.shuffle(shuffled_pairs)
    
    # Calculate split point
    total_pairs = len(shuffled_pairs)
    train_count = int(total_pairs * train_split)
    
    print(f"\nSplitting {total_pairs} pairs:")
    print(f"  Training: {train_count} pairs ({train_split*100:.0f}%)")
    print(f"  Validation: {total_pairs - train_count} pairs ({(1-train_split)*100:.0f}%)")
    
    # Copy training files
    print(f"\nCopying training files...")
    for i, (image_path, annotation_path) in enumerate(shuffled_pairs[:train_count]):
        # Copy image
        shutil.copy2(image_path, train_dir / "images" / image_path.name)
        # Copy annotation
        shutil.copy2(annotation_path, train_dir / "labels" / annotation_path.name)
        
        if (i + 1) % 50 == 0 or (i + 1) == train_count:
            print(f"  Copied {i + 1}/{train_count} training pairs")
    
    # Copy validation files
    print(f"\nCopying validation files...")
    valid_pairs = shuffled_pairs[train_count:]
    for i, (image_path, annotation_path) in enumerate(valid_pairs):
        # Copy image
        shutil.copy2(image_path, valid_dir / "images" / image_path.name)
        # Copy annotation
        shutil.copy2(annotation_path, valid_dir / "labels" / annotation_path.name)
        
        if (i + 1) % 50 == 0 or (i + 1) == len(valid_pairs):
            print(f"  Copied {i + 1}/{len(valid_pairs)} validation pairs")
    
    return train_count, len(valid_pairs)

def create_dataset_yaml(output_dir: Path, dataset_name: str, class_names: List[str] = None):
    """
    Create a dataset.yaml file for YOLOv8 training.
    
    Args:
        output_dir: Dataset directory
        dataset_name: Name of the dataset
        class_names: List of class names (default: ['ball'])
    """
    if class_names is None:
        class_names = ['ball']
    
    dataset_config = {
        'path': str(output_dir.absolute()),
        'train': 'train/images',
        'val': 'valid/images',
        'nc': len(class_names),
        'names': class_names
    }
    
    yaml_path = output_dir / 'dataset.yaml'
    with open(yaml_path, 'w') as f:
        yaml.dump(dataset_config, f, default_flow_style=False, sort_keys=False)
    
    print(f"\nCreated dataset configuration: {yaml_path}")
    print("Dataset YAML contents:")
    print(f"  path: {dataset_config['path']}")
    print(f"  train: {dataset_config['train']}")
    print(f"  val: {dataset_config['val']}")
    print(f"  nc: {dataset_config['nc']}")
    print(f"  names: {dataset_config['names']}")

def print_summary(dataset_name: str, output_dir: Path, train_count: int, valid_count: int, 
                 tags: List[str]):
    """Print a summary of the dataset preparation."""
    total_count = train_count + valid_count
    train_percent = (train_count / total_count * 100) if total_count > 0 else 0
    valid_percent = (valid_count / total_count * 100) if total_count > 0 else 0
    
    print(f"\n" + "="*60)
    print(f"DATASET PREPARATION COMPLETE")
    print(f"="*60)
    print(f"Dataset Name: {dataset_name}")
    print(f"Output Directory: {output_dir}")
    print(f"Source Tags: {', '.join(tags)}")
    print(f"")
    print(f"Dataset Statistics:")
    print(f"  Total Images: {total_count}")
    print(f"  Training: {train_count} ({train_percent:.1f}%)")
    print(f"  Validation: {valid_count} ({valid_percent:.1f}%)")
    print(f"")
    print(f"Directory Structure:")
    print(f"  {output_dir}/")
    print(f"  ├── train/")
    print(f"  │   ├── images/ ({train_count} files)")
    print(f"  │   └── labels/ ({train_count} files)")
    print(f"  ├── valid/")
    print(f"  │   ├── images/ ({valid_count} files)")
    print(f"  │   └── labels/ ({valid_count} files)")
    print(f"  └── dataset.yaml")
    print(f"")
    print(f"Ready for YOLOv8 training!")
    print(f"Use: yolo train data={output_dir}/dataset.yaml model=yolov8n.pt")

def main():
    parser = argparse.ArgumentParser(
        description="Prepare annotated dataset for YOLOv8 training with 80/20 split",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Create V1 generalist dataset from specific tags
  python3 scripts/prepare_dataset.py --dataset-name V1_generalist --tags good_lighting clear_balls multiple_balls

  # Create V2 specialized dataset from high-quality tags
  python3 scripts/prepare_dataset.py --dataset-name V2_specialized --tags perfect_lighting high_contrast professional_setup

  # Include all available tags
  python3 scripts/prepare_dataset.py --dataset-name complete_dataset --tags all

  # Custom split ratio (e.g., 70/30)
  python3 scripts/prepare_dataset.py --dataset-name custom_split --tags all --train-split 0.7

  # Custom source and output directories
  python3 scripts/prepare_dataset.py --source-dir /path/to/annotations --output-dir /path/to/datasets --dataset-name my_dataset --tags all
        """
    )
    
    parser.add_argument('--dataset-name', required=True,
                       help='Name of the dataset (e.g., V1_generalist, V2_specialized)')
    
    parser.add_argument('--tags', nargs='+', required=True,
                       help='Tag directories to include, or "all" for all directories')
    
    parser.add_argument('--source-dir', type=Path, 
                       default=Path('2_tagged_and_annotated'),
                       help='Source directory containing tagged annotations (default: 2_tagged_and_annotated)')
    
    parser.add_argument('--output-dir', type=Path,
                       default=Path('3_training_datasets'),
                       help='Output directory for training datasets (default: 3_training_datasets)')
    
    parser.add_argument('--train-split', type=float, default=0.8,
                       help='Fraction of data for training (default: 0.8 for 80/20 split)')
    
    parser.add_argument('--class-names', nargs='+', default=['ball'],
                       help='Class names for the dataset (default: ball)')
    
    parser.add_argument('--seed', type=int, default=42,
                       help='Random seed for reproducible splits (default: 42)')
    
    parser.add_argument('--dry-run', action='store_true',
                       help='Show what would be done without actually copying files')
    
    args = parser.parse_args()
    
    # Set random seed for reproducible results
    random.seed(args.seed)
    
    # Validate arguments
    if not 0.1 <= args.train_split <= 0.9:
        print("Error: train-split must be between 0.1 and 0.9")
        sys.exit(1)
    
    # Setup paths
    source_dir = args.source_dir
    output_dir = args.output_dir / args.dataset_name
    
    # Check source directory exists
    if not source_dir.exists():
        print(f"Error: Source directory '{source_dir}' does not exist")
        print(f"Please create the directory and add your tagged annotations")
        sys.exit(1)
    
    # Check if output directory already exists
    if output_dir.exists() and not args.dry_run:
        response = input(f"Output directory '{output_dir}' already exists. Overwrite? (y/N): ")
        if response.lower() != 'y':
            print("Aborted.")
            sys.exit(1)
        shutil.rmtree(output_dir)
    
    print(f"Dataset Preparation Script")
    print(f"=" * 40)
    print(f"Dataset Name: {args.dataset_name}")
    print(f"Source Directory: {source_dir}")
    print(f"Output Directory: {output_dir}")
    print(f"Tags: {', '.join(args.tags)}")
    print(f"Train/Valid Split: {args.train_split:.1f}/{1-args.train_split:.1f}")
    print(f"Random Seed: {args.seed}")
    print(f"Class Names: {', '.join(args.class_names)}")
    
    if args.dry_run:
        print(f"DRY RUN MODE - No files will be copied")
    
    # Find all image/annotation pairs
    print(f"\n" + "="*40)
    pairs = find_image_annotation_pairs(source_dir, args.tags)
    
    if not pairs:
        print("Error: No image/annotation pairs found!")
        print("Make sure your source directory contains:")
        print("  - Subdirectories with tag names")
        print("  - Image files (.jpg, .png, etc.)")
        print("  - Corresponding .txt annotation files")
        sys.exit(1)
    
    print(f"\nFound {len(pairs)} total image/annotation pairs")
    
    if args.dry_run:
        # Calculate what would be done
        train_count = int(len(pairs) * args.train_split)
        valid_count = len(pairs) - train_count
        print(f"\nDRY RUN: Would create:")
        print(f"  Training: {train_count} pairs")
        print(f"  Validation: {valid_count} pairs")
        print(f"  Output: {output_dir}")
        return
    
    # Create dataset structure
    print(f"\n" + "="*40)
    print("Creating dataset structure...")
    train_dir, valid_dir = create_dataset_structure(output_dir)
    
    # Copy files with 80/20 split
    train_count, valid_count = copy_files(pairs, train_dir, valid_dir, args.train_split)
    
    # Create dataset.yaml
    create_dataset_yaml(output_dir, args.dataset_name, args.class_names)
    
    # Print summary
    print_summary(args.dataset_name, output_dir, train_count, valid_count, args.tags)

if __name__ == "__main__":
    main()