# scripts/create_yaml.py
import yaml
import argparse
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description="Create a dataset.yaml file for a YOLO dataset.")
    parser.add_argument("dataset_dir", type=str, help="Path to the root of the dataset directory (e.g., data/3_training_datasets/V2_with_hands).")
    parser.add_argument("class_names", nargs='+', help="A space-separated list of class names in order (e.g., led_on led_off dropped_ball hand).")
    args = parser.parse_args()

    dataset_path = Path(args.dataset_dir).resolve()
    class_names = args.class_names
    num_classes = len(class_names)

    yaml_data = {
        'path': str(dataset_path),  # This path will be updated in Colab
        'train': 'train/images',
        'val': 'valid/images',
        'nc': num_classes,
        'names': class_names
    }

    yaml_file_path = dataset_path / 'dataset.yaml'

    with open(yaml_file_path, 'w') as f:
        yaml.dump(yaml_data, f, sort_keys=False)

    print(f"✅ Successfully created '{yaml_file_path}' with {num_classes} classes:")
    print(yaml.dump(yaml_data))

if __name__ == "__main__":
    main()