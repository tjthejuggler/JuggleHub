import os

def batch_rename_files_and_directory(directory, suffix):
    """
    Renames a directory and all files within it by adding a suffix.

    Args:
        directory (str): The path to the directory to rename.
        suffix (str): The suffix to add to the directory and filenames.
    """
    new_directory_path = directory + suffix
    try:
        os.rename(directory, new_directory_path)
        print(f"Renamed directory '{directory}' to '{new_directory_path}'")
    except FileNotFoundError:
        print(f"Error: Directory not found at '{directory}'")
        return
    except OSError as e:
        print(f"Error renaming directory {directory}: {e}")
        return

    try:
        all_files = os.listdir(new_directory_path)
    except FileNotFoundError:
        print(f"Error: Directory not found at '{new_directory_path}'")
        return

    for filename in all_files:
        basename, extension = os.path.splitext(filename)
        new_filename = f"{basename}{suffix}{extension}"
        old_filepath = os.path.join(new_directory_path, filename)
        new_filepath = os.path.join(new_directory_path, new_filename)
        try:
            os.rename(old_filepath, new_filepath)
            print(f"Renamed '{filename}' to '{new_filename}'")
        except OSError as e:
            print(f"Error renaming file {filename}: {e}")

    print("\nBatch renaming complete.")

if __name__ == "__main__":
    target_directory = "/home/twain/Downloads/output"
    rename_suffix = "_batch1"
    batch_rename_files_and_directory(target_directory, rename_suffix)