# Hairdryer App

Provides the initial GPIO bring-up for the Hairdryer hardware on the
`touch_lcd_1_28` board. It has no display, LVGL, or app-specific managed
dependency.

## Required Capabilities

The selected board must define `BOARD_FEATURE_HAIRDRYER` and the Hairdryer WS2812
and button GPIO assignments. Compatibility is declared in each board's
`supported_apps.txt`; only `touch_lcd_1_28` enables this app.

## Behavior

- GPIO15 is configured as a WS2812 data output and held low. No light effect or
  WS2812 frame transmission is implemented yet.
- GPIO18 is the active-low mode button. One `BUTTON_MODE_CLICK` event is emitted
  after each complete press and release.
- GPIO17 is the active-low cold button. It emits independent
  `BUTTON_COLD_PRESSED` and `BUTTON_COLD_RELEASED` events.
- GPIO33 and GPIO21 form a three-position switch:
  - `0/1`: `BUTTON_SWITCH_OFF`
  - `1/1`: `BUTTON_SWITCH_LOW`
  - `1/0`: `BUTTON_SWITCH_HIGH`
- The invalid `0/0` switch combination is ignored and the previous valid state
  is retained. Switch events are emitted only after the state changes.
- All inputs use internal pull-ups and a 30 ms debounce period.

## Task Model

The `hairdryer_buttons` task scans all four GPIOs every 10 ms and posts the three
independently named event categories to one FreeRTOS queue. The
`hairdryer_dispatch` task receives them and calls the matching placeholder
function under `lightring/`. No runtime shutdown path is required.

## Model Contract

There is no LVGL model. The light-ring handlers are placeholders for the
effects that will be added later.

## Host Check

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I main/app_hairdryer \
  main/app_hairdryer/test/test_buttons_host.c \
  main/app_hairdryer/hairdryer_buttons.c \
  -o /tmp/hairdryer_buttons_test &&
  /tmp/hairdryer_buttons_test
```

## Device Checks

On the target board, verify each event name, the 30 ms debounce behavior, no
mode event until release, both cold-button edge events, each switch state, and
that the temporary `0/0` switch combination generates no event.
