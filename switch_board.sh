#!/usr/bin/env bash
# Two-stage selector for the multi-board / multi-app ESP-IDF project.
#   Step 1: choose the target board.
#   Step 2: choose the application.
# Press 'q' at any prompt to quit, or Enter to keep the current selection.
set -euo pipefail
cd "$(dirname "$0")"

BOARDS=(amoled_175 amoled_206 touch_lcd_1_28)
BOARD_DESC=(
    "Waveshare ESP32-S3 Touch AMOLED 1.75\""
    "Waveshare ESP32-S3 Touch AMOLED 2.06\""
    "Waveshare ESP32-S3 Touch LCD 1.28\" (round, IMU-only)"
)
PROJECTS=(slide_player salary_cat acc_data battery_monitor lightring_button)
PROJ_DESC=(
    "PNG slideshow (touch gestures)"
    "Salary cat (GIF + MP3 from SD)"
    "Accelerometer logger (IMU + PMU)"
    "Battery / PMU monitor (AXP2101)"
    "WS2812 light ring + button"
)

read_cur() { if [ -f "$1" ]; then tr -d '[:space:]' <"$1"; else printf '%s' "$2"; fi; }
cur_board="$(read_cur .board "${BOARDS[0]}")"
cur_proj="$(read_cur .lvgl_project "${PROJECTS[0]}")"

load_supported_apps() {
    supported_file="boards/$1/supported_apps.txt"
    if [ ! -f "$supported_file" ]; then
        echo "Missing board compatibility file: $supported_file" >&2
        exit 1
    fi

    BOARD_SUPPORTED_APPS=()
    while IFS= read -r supported_app || [ -n "$supported_app" ]; do
        case "$supported_app" in
            "" | \#*) continue ;;
        esac
        BOARD_SUPPORTED_APPS+=("$supported_app")
    done <"$supported_file"

    if [ "${#BOARD_SUPPORTED_APPS[@]}" -eq 0 ]; then
        echo "No supported apps declared in $supported_file" >&2
        exit 1
    fi
}

is_app_compatible() {
    candidate="$1"
    for supported_app in "${BOARD_SUPPORTED_APPS[@]}"; do
        [ "$supported_app" = "$candidate" ] && return 0
    done
    return 1
}

# --- Step 1: board ----------------------------------------------------------
echo "Step 1/2 - Select board (current: $cur_board)"
for i in "${!BOARDS[@]}"; do
    mark="  "; [ "${BOARDS[$i]}" = "$cur_board" ] && mark="=>"
    printf "  %s %d) %-12s %s\n" "$mark" "$((i + 1))" "${BOARDS[$i]}" "${BOARD_DESC[$i]}"
done
board="$cur_board"
while true; do
    printf "  number / Enter=keep / q=quit: "
    read -r c || c="q"
    case "$c" in
        q | Q) echo "Aborted."; exit 0 ;;
        "") break ;;
        *)
            if [ "$c" -eq "$c" ] 2>/dev/null && [ "$c" -ge 1 ] && [ "$c" -le "${#BOARDS[@]}" ]; then
                board="${BOARDS[$((c - 1))]}"; break
            fi
            echo "  invalid: '$c'" ;;
    esac
done

load_supported_apps "$board"
project="$cur_proj"
if ! is_app_compatible "$project"; then
    project="${BOARD_SUPPORTED_APPS[0]}"
    echo "  '$cur_proj' is incompatible with $board; defaulting to '$project'."
fi

# --- Step 2: application ----------------------------------------------------
echo
echo "Step 2/2 - Select application (current: $cur_proj)"
for i in "${!PROJECTS[@]}"; do
    app_label="${PROJECTS[$i]}"
    is_app_compatible "${PROJECTS[$i]}" || app_label="$app_label (Incompatible)"
    mark="  "; [ "${PROJECTS[$i]}" = "$project" ] && mark="=>"
    printf "  %s %d) %-31s %s\n" "$mark" "$((i + 1))" "$app_label" "${PROJ_DESC[$i]}"
done
while true; do
    printf "  number / Enter=keep / q=quit: "
    read -r c || c="q"
    case "$c" in
        q | Q) echo "Aborted."; exit 0 ;;
        "") break ;;
        *)
            if [ "$c" -eq "$c" ] 2>/dev/null && [ "$c" -ge 1 ] && [ "$c" -le "${#PROJECTS[@]}" ]; then
                candidate="${PROJECTS[$((c - 1))]}"
                if is_app_compatible "$candidate"; then
                    project="$candidate"; break
                fi
                echo "  incompatible: '$candidate' is not supported on '$board'"
                continue
            fi
            echo "  invalid: '$c'" ;;
    esac
done

printf '%s\n' "$board" >.board
printf '%s\n' "$project" >.lvgl_project

# Each app's entry point lives in main/app_<project>/app_<project>.c
app_entry="main/app_${project}/app_${project}.c"
[ -f "$app_entry" ] || echo "  warning: entry point not found: $app_entry"

echo
echo "Selected: board=$board  project=$project"
echo "App entry: $app_entry"
echo "(.board / .lvgl_project updated - they are the single source of truth)"
echo
echo "Build:           idf.py build"
echo "Flash & monitor: idf.py -p <PORT> flash monitor"
echo "Or just use the VS Code ESP-IDF 'Build' button (shared build/ dir)."
