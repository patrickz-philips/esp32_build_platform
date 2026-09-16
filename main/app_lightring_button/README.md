# Light Ring Button App

Drives a 27-pixel WS2812 light ring from GPIO16 and reads an active-low button
from GPIO18. The app is compatible only with the standard and `-B`
`amoled_175` variants. GPIO18 conflicts with the optional GNSS routing on the
`-G` variant.

## Required Capabilities

The selected board must define `BOARD_HAS_LIGHTRING_BUTTON`,
`BOARD_LIGHTRING_GPIO`, and `BOARD_BUTTON_GPIO`. Compatibility is declared by
each board's `supported_apps.txt`; only `amoled_175` enables this app.

The app has no display, LVGL, or app-specific managed dependency. It drives the
ESP32-S3 RMT TX peripheral directly.

## Behavior

- The ring starts off.
- Pressing the button while off expands the current palette across the ring.
- Pressing while on starts a change to the next palette.
- Releasing before 300 ms continues the current collapse smoothly to off.
- Releasing from 300 ms through 749 ms fades the two edge LEDs off.
- Releasing at or after 750 ms commits the next palette and expands it.

The button is sampled every 10 ms and must remain stable for 30 ms. The GPIO18
internal pull-up is enabled, so low means pressed and high means released.

## Initialization and Tasks

Initialization configures GPIO18, creates an ESP32-S3 RMT TX channel on GPIO16,
sends an all-off frame, and starts the render task. RMT resources are released
if the initial frame or task creation fails.

| Task | Stack | Priority | Responsibility |
|:-----|:------|:---------|:---------------|
| `main` | ESP-IDF default | ESP-IDF default | Poll and debounce GPIO18; translate button edges to light-domain events |
| `lightring` | 4096 bytes | 5 | Render and transmit interruptible 15 ms RGB animation frames |

Both tasks and the RMT device live for the firmware lifetime. No runtime
shutdown path is required by this app.

## Model Contract

There is no LVGL model. The app calls the typed `lightring_*` domain API; it
does not access renderer state or frame-buffer internals.

## Device Checks

The build validates component and RMT API wiring only. On the standard 1.75-inch
board, verify GPIO levels, RGB byte order, all 27 LED positions, palette order,
the 30 ms debounce behavior, and the 750 ms short/long press boundary.