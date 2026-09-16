#!/usr/bin/env bash
set -u

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if ! command -v python3 >/dev/null 2>&1; then
    printf 'Python 3 is required to run Slide Player Control.\n' >&2
    read -r -p 'Press Return to close.'
    exit 1
fi

python3 "$script_dir/slide_player_control.py" "$@"
exit_code=$?
if (( exit_code != 0 )); then
    read -r -p 'Press Return to close.'
fi
exit "$exit_code"