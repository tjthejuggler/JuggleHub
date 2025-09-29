import os

def delete_unmatched_txt_files(directory):
    """
    Deletes .txt files in a directory that do not have a matching .jpg file.

    Args:
        directory (str): The path to the directory to clean up.
    """
    try:
        all_files = os.listdir(directory)
    except FileNotFoundError:
        print(f"Error: Directory not found at '{directory}'")
        return

    jpg_files = {os.path.splitext(f)[0] for f in all_files if f.lower().endswith('.jpg')}
    txt_files = [f for f in all_files if f.lower().endswith('.txt')]

    deleted_count = 0
    for txt_file in txt_files:
        txt_basename = os.path.splitext(txt_file)[0]
        if txt_basename not in jpg_files:
            file_path = os.path.join(directory, txt_file)
            try:
                os.remove(file_path)
                print(f"Deleted: {file_path}")
                deleted_count += 1
            except OSError as e:
                print(f"Error deleting file {file_path}: {e}")

    print(f"\nCleanup complete. Deleted {deleted_count} orphaned .txt files.")

if __name__ == "__main__":
    target_directory = "/home/twain/Downloads/output"
    delete_unmatched_txt_files(target_directory)