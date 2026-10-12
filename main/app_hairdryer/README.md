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
- GPIO33, GPIO21, GPIO18, and GPIO17 are configured as inputs with internal
  pull-ups for active-low buttons.
- All four raw GPIO levels are logged once per second.

## Task Model

The ESP-IDF `main` task configures all five GPIOs and runs the one-second polling
loop. No additional task or runtime shutdown path is required.

## Model Contract

There is no LVGL model. This app currently performs GPIO bring-up only.

## Device Checks

The build validates the GPIO driver wiring only. On the target board, verify
that GPIO15 stays low, each unpressed button reads high, and grounding each
button input makes only its corresponding logged level read low.
