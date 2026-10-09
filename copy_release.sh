#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
board_file="${script_dir}/.board"
app_file="${script_dir}/.lvgl_project"
release_dir="${script_dir}/release"

if [[ ! -f "${board_file}" || ! -f "${app_file}" ]]; then
    echo "Error: select a board and app before copying a release." >&2
    exit 1
fi

board="$(tr -d '[:space:]' < "${board_file}")"
app="$(tr -d '[:space:]' < "${app_file}")"
build_dir="${script_dir}/build/${board}_${app}"
flasher_args="${build_dir}/flasher_args.json"

if [[ ! -f "${flasher_args}" ]]; then
    echo "Error: build output not found: ${flasher_args}" >&2
    echo "Build ${board} + ${app} first." >&2
    exit 1
fi

python3 - "${build_dir}" "${flasher_args}" "${release_dir}" <<'PY'
import json
import os
import shutil
import sys

build_dir, flasher_args, release_dir = sys.argv[1:]
with open(flasher_args, encoding="utf-8") as handle:
    flash_files = json.load(handle).get("flash_files", {})

if not flash_files:
    raise SystemExit(f"Error: no flash files found in {flasher_args}")

entries = sorted(flash_files.items(), key=lambda item: int(item[0], 16))
sources = [(offset, path, os.path.join(build_dir, path)) for offset, path in entries]
missing = [source for _, _, source in sources if not os.path.isfile(source)]
if missing:
    raise SystemExit("Error: missing build output:\n" + "\n".join(missing))

names = [os.path.basename(path) for _, path, _ in sources]
if len(names) != len(set(names)):
    raise SystemExit("Error: flash files contain duplicate basenames")

shutil.rmtree(release_dir, ignore_errors=True)
os.makedirs(release_dir)

for (offset, path, source), name in zip(sources, names):
    shutil.copy2(source, os.path.join(release_dir, name))
    print(f"Copied: {path}")

with open(os.path.join(release_dir, "load.md"), "w", encoding="utf-8") as handle:
    for (offset, _, _), name in zip(sources, names):
        handle.write(f"{name}    {offset}\n")

print(f"Generated: {os.path.join(release_dir, 'load.md')}")
print(f"Done: copied {len(sources)} firmware files to {release_dir}")
PY
