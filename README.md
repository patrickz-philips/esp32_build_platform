# ESP-IDF Multi-Board LVGL Demos

This repository hosts ESP-IDF + LVGL applications for several ESP32-S3 and
ESP32-C6 display boards. The slide player loads numbered PNG or GIF files from
micro-SD and supports either touch gestures or a board button.

## Architecture Overview

The project is organized as a small layered application:

1. Platform layer (BSP and board drivers)
- Board init and display bring-up are handled by a vendor or project-owned BSP.
- SD card is mounted over SDSPI.

2. Runtime services
- LVGL image decoder is initialized via `esp_lv_decoder`.
- Filesystem path for UI assets is mapped to `A:/sdcard` (LVGL path style).

3. UI / feature layer (`slide_player`)
- Creates the LVGL screen and widgets.
- Handles touch gestures or button-driven advance.
- Probes numbered PNG files first, then GIF files.

## Runtime Flow

1. `app_main()` starts board display and acquires the display lock.
2. `slide_player_runtime_init()` mounts SD card and initializes LVGL decoder.
3. `slide_player_ui_init()` builds the UI and displays the first slide.
4. A gesture or button press updates the slide index and refreshes the media.

## Project Structure

```text
.
|-- CMakeLists.txt                # Project entry (resolves BOARD + LVGL_PROJECT)
|-- switch_board.sh               # Two-stage selector (board, then app)
|-- boards/
|   |-- amoled_175/               # AMOLED 1.75" board profile
|   |-- amoled_206/               # AMOLED 2.06" board profile
|   |-- touch_lcd_1_28/           # Round LCD board profile
|   `-- esp32_c6_lcd_0_96/        # ESP32-C6 ST7735 board profile and BSP
|-- components/
|   |-- XPowersLib/               # AXP2101 driver
|   `-- pmu_power/                # Reusable PMU service
|-- lvgl/                         # LVGL applications (git submodules)
|   |-- slide_player/             # PNG/GIF slideshow
|   |-- acc_data/                 # accelerometer logger
|   |-- salary_cat/               # GIF + MP3 player
|   `-- battery_monitor/          # battery / PMU monitor
|-- main/
|   |-- CMakeLists.txt            # Generic main component during migration
|   |-- idf_component.yml         # Common component dependencies
|   |-- app_acc_data/             # acc_data entry, IMU, and SD writer
|   |-- app_salary_cat/           # salary_cat entry and audio adapter
|   |-- app_slide_player/         # slide_player component migration
|   |-- app_battery_monitor/      # battery_monitor component migration
|   `-- app_lightring_button/     # WS2812 ring and button app (no LVGL)
|-- sdkconfig.defaults            # Common configuration
`-- sdkconfig.<board>             # Generated per-board config (gitignored)
```

The `lvgl/*` applications are tracked as git submodules; clone with
`git clone --recurse-submodules`, or after cloning run
`git submodule update --init --recursive`.

## Key Dependencies

- ESP-IDF: `5.5.4`
- LVGL: `9.4.x`
- Board support: Waveshare BSPs or project-owned board components
- LVGL decoder: `espressif/esp_lv_decoder`

See `main/idf_component.yml` and `dependencies.lock` for exact dependency definitions.

## Build and Flash

A build is defined by two independent selections: the **board** (hardware) and
the **application** (stored in the legacy `LVGL_PROJECT` variable). Use the
two-stage selector:

```bash
./switch_board.sh
#   Step 1/2 - Select board          (shows current, '=>' marks it)
#   Step 2/2 - Select application    (shows current)
#   Enter = keep current, q = quit
```

It writes `./.board` and `./.lvgl_project`, then prints the ready-to-run commands:

```bash
idf.py -B build/amoled_175_slide_player build
idf.py -B build/amoled_175_slide_player -p <PORT> flash monitor
```

Each board+project pair uses its own build directory so configs never mix.

## Multi-board / Multi-app Support

Both `BOARD` and `LVGL_PROJECT` resolve in the same priority order: environment
variable -> persisted file (`./.board` / `./.lvgl_project`) -> default
(`amoled_175` / `slide_player`).

| Board id | Hardware |
|----------|----------|
| `amoled_175` | Waveshare ESP32-S3 Touch AMOLED 1.75" |
| `amoled_206` | Waveshare ESP32-S3 Touch AMOLED 2.06" |
| `touch_lcd_1_28` | Waveshare ESP32-S3 Touch LCD 1.28" |
| `esp32_c6_lcd_0_96` | Spotpear ESP32-C6 LCD 0.96" (GPIO9 button, no touch) |

| App id | App | Recommended board |
|--------------|-----|-------------------|
| `slide_player` | PNG/GIF slideshow (touch or button) | `amoled_175`, `esp32_c6_lcd_0_96` |
| `salary_cat` | GIF + MP3 player | `amoled_206` |
| `acc_data` | Accelerometer logger (IMU + PMU) | `amoled_206` |
| `battery_monitor` | Battery / PMU monitor (AXP2101) | `amoled_206` |
| `lightring_button` | WS2812 light ring + active-low button | `amoled_175` |

Each board is self-contained under `boards/<board>/`:

```text
boards/<board>/
|-- CMakeLists.txt      # board component (carries managed deps)
|-- idf_component.yml   # board-specific managed dependencies
|-- board_config.h      # capability flags + pin map
|-- sdkconfig.board     # board sdkconfig overrides (flash size, partition file)
`-- partitions.csv      # board partition table
```

The top-level `CMakeLists.txt` reads `BOARD` (wires `EXTRA_COMPONENT_DIRS`,
per-board `SDKCONFIG` and layered `SDKCONFIG_DEFAULTS`) and `LVGL_PROJECT`.
`main/CMakeLists.txt` picks the entry file (`app_<project>.c`) and application
sources from `lvgl/<project>/`.


## Slide Asset Requirements

- Place slide images on SD card under `/sdcard`.
- Use positive integer filenames without leading zeroes and with a `.png` or
  `.gif` extension. The highest numbered file defines the slide count; PNG takes
  precedence when both formats exist for the same number.
- Touch behavior:
  - Swipe left: next slide
  - Swipe right: previous slide
- On `esp32_c6_lcd_0_96`, press GPIO9 to advance; the highest slide wraps to slide 1.
- Serial control accepts `next`, `last`, or a slide number followed by Enter.
  On macOS, double-click `mac-app/slide-player.command` to map the arrow keys and
  numeric input to these commands.

## Current Design Notes

- UI updates run through LVGL callbacks; non-touch button requests take the
  BSP display lock before changing the slide.
- Performance logs are emitted on each slide switch to help profile decode and memory usage.
- Asset C files in `slide_player/assets/*.c` are not compiled by default; media
  is loaded from SD at runtime.

## Future Improvement Ideas

- Add rollback cleanup if any runtime init step fails.
- Move machine-specific `.vscode` settings to user-local config.
- Reduce global warning suppression and scope it to specific files only.
