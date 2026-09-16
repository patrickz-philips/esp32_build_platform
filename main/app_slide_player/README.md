# Slide Player App

Displays numbered PNG or animated GIF slides from the SD card. At startup, the
app scans the SD root and uses the highest numbered file as the slide count.
Touch boards change slides with horizontal gestures. Non-touch button boards
advance on a button press and wrap from the highest number to slide 1. For each
positive integer filename without leading zeroes, the app loads
`/sdcard/<number>.png` first and falls back to
`/sdcard/<number>.gif` when the PNG is absent.

## Required Capabilities

The app requires a display, SD storage, and either touch or button input. Compatibility is
declared by each board's `supported_apps.txt`; `board_config.h` provides a
second compile-time capability check. SD wiring and mounting are owned by the
selected board BSP.

`app_slide_player` is an independent selected component. Its app-only image
dependency is `espressif/esp_lv_decoder` 0.4.x.

## Initialization

Initialization starts the BSP display, mounts the SD card, initializes the LVGL
PNG decoder, creates the one-entry request queue and SD reader task, then creates
the UI while holding the BSP display lock. LVGL's native GIF widget is enabled
through `sdkconfig.app`. A required failure stops later stages and rolls back the
task, queue, decoder, and SD mount as applicable.

## Task Model

| Task | Stack | Priority | Responsibility |
|:-----|:------|:---------|:---------------|
| `slide_sd_reader` | 4096 bytes | 4 | Validate the latest requested slide file and post a typed result to the LVGL model |
| `slide_button` | 2048 bytes | 4 | Debounce the optional board button and request the next slide |
| `slide_serial` | 3072 bytes | 4 | Read newline-delimited slide commands from the primary console |

The BSP owns the LVGL task. A one-entry overwrite queue coalesces rapid gestures
so stale slide requests do not accumulate. The UI also drops stale results by
request ID. On boards where the display and SD card share an SPI host, the
reader holds the display lock and drains queued LCD transfers before probing a
file.

## LVGL Model Contract

`slide_player.h` defines the typed bridge. The UI calls the app-provided
`request_slide` callback with a slide index and request ID. The reader task posts
`slide_player_load_result_t` through `slide_player_post_load_result()`; an LVGL
timer consumes that queue and is the only asynchronous path that mutates image
or label widgets.

Button boards call `slide_player_show_next()` while holding the BSP display
lock. The request wraps modulo the configured slide count. If both the PNG and
GIF for a requested slide are unavailable, the same button request immediately
falls back to the first slide while touch gestures keep their existing bounded
behavior.

The top-level frame reads the active display resolution and has no BSP header or
board-specific panel dimensions.

## Serial Control

The primary console accepts one command per line at 115200 baud:

- `next`: show the next slide and wrap at the end.
- `last`: show the previous slide and wrap at the beginning.
- Any number up to the highest numbered SD asset: show that slide.

On macOS, double-click `mac-app/slide-player.command` in Finder, or run it with
an explicit port as `./mac-app/slide-player.command /dev/cu.usbmodemXXXX`. The
left and right arrow keys send `last` and `next`; enter a number followed by
Return to open that slide, and press Escape to quit.

## Device Checks

Builds validate component wiring only. On each board, verify SD mount behavior,
PNG and GIF decoding, animation playback, left/right gestures, boundary
handling, rapid gesture coalescing, missing-file recovery, and full-screen
layout.