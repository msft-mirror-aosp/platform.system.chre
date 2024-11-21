"""Finds header files in target_platform directories that should not be

Some platform implementations have improperly used target_platform when they
should instead use platform/<platform_name>. This script helps identify those
files.

Example usage:

  python find_improper_target_platform_files.py -p ../platform -c ../platform/include
"""
import argparse
import os
import sys

def find_target_platform_files(directories):
  """
  Lists all files recursively in the given directories and filters to those with
  "target_platform" in their path.

  Args:
    directories: A list of directories to search.

  Returns:
    A list of file paths that contain "target_platform" in their path.
  """
  target_files = []
  for directory in directories:
    for root, _, files in os.walk(directory):
      for file in files:
        file_path = os.path.join(root, file)
        if "target_platform" in file_path:
          target_files.append(file_path)
  return target_files

def find_unincluded_files(target_files, include_directories):
  """
  Flags target_platform files that are not included in any C/C++ #include
  statements within files in the include_directories.

  Args:
    target_files: A list of file paths to check.
    include_directories: A list of directories containing files with
                         #include statements.

  Returns:
    A list of target_platform file paths that are not included.
  """
  unincluded_files = []
  included_files = set()

  for directory in include_directories:
    for root, _, files in os.walk(directory):
      for file in files:
        file_path = os.path.join(root, file)
        try:
          with open(file_path, "r") as f:
            for line in f:
              if line.startswith("#include") and "target_platform" in line:
                # Extract the included file name, accounting for variations in
                # include syntax (e.g., quotes vs. angle brackets)
                included_file = line.split()[1].strip('"<>')
                included_files.add(included_file)
        except UnicodeDecodeError:
          # Skip non-text files
          continue

  print(f"target_platform files referenced in {include_directories}:")
  for file in sorted(included_files):
    print(f"  {file}")


  for target_file in target_files:
    target_file_name = "target_platform/" + os.path.basename(target_file)
    found = False
    for included_file in included_files:
      if included_file.endswith(target_file_name):
        found = True
        break
    if not found:
      unincluded_files.append(target_file)

  return unincluded_files

if __name__ == "__main__":
  parser = argparse.ArgumentParser(description="Find files in target_platform directories which are not included from common code.")
  parser.add_argument("-p", "--platform", nargs="+", required=True, help="Directories containing target_platform subdirectories")
  parser.add_argument("-c", "--common", nargs="+", required=True, help="Directories containing facades to platform code - all target_platform headers must appear in an #include statement in a file here")
  args = parser.parse_args()

  target_directories = args.platform
  include_directories = args.common

  target_files = find_target_platform_files(target_directories)
  unincluded_files = find_unincluded_files(target_files, include_directories)

  if len(unincluded_files):
    print("\nThe following target_platform files do not appear in the list above:")
    for file in sorted(unincluded_files):
      print(f"  {file}")
  else:
    print("All target_platform files are included.")
