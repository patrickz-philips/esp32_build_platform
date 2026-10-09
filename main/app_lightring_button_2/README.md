# Light Ring Button 2 App

Drives the external 27-pixel WS2812 V-shaped light ring on GPIO16 and reads the
active-low button on GPIO18. The app is compatible only with the standard and
`-B` `amoled_175` variants. GPIO18 conflicts with optional GNSS routing on the
`-G` variant.

## Required Capabilities

The selected board must define `BOARD_HAS_LIGHTRING_BUTTON`,
`BOARD_LIGHTRING_GPIO`, and `BOARD_BUTTON_GPIO`. Compatibility is declared by
each board's `supported_apps.txt`; only `amoled_175` enables this app.

The app has no display, LVGL, or app-specific managed dependency. Its bundled
light-ring code drives the ESP32-S3 RMT TX peripheral directly.

## Behavior

- The ring starts solid red.
- Stable modes cycle red, green, blue, then red again.
- A button press starts contiguous red, green, and blue bars moving from the
  bottom toward both upper arms. The gapless pattern repeats every 400 ms.
- Releasing before 750 ms immediately restores the original solid mode color.
- At 750 ms, without waiting for release, the ring immediately switches to the
  next solid mode color.

The button is sampled every 10 ms and must remain stable for 30 ms. The GPIO18
internal pull-up is enabled, so low means pressed and high means released.

## Initialization and Task Model

The ESP-IDF `main` task configures the button and RMT output, displays the solid
red startup frame, polls the button, renders animation frames, and transmits
changed effects. No additional task is created. RMT resources are released if
startup output or a later frame transmission fails.

## Model Contract

There is no LVGL model. The app owns its button state and pure framebuffer
renderer and uses the shared typed `ws2812_*` transport API.

## Host Check

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I main/app_lightring_button_2 \
  -I main/app_lightring_button_2/lightring/include \
  main/app_lightring_button_2/test/test_effects_host.c \
  main/app_lightring_button_2/lightring_button_2_effects.c \
  -o /tmp/lightring_button_2_test &&
  /tmp/lightring_button_2_test
```

## Device Checks

The build and host check do not validate physical RGB byte order, GPIO levels,
perceived brightness, or timing. On the standard 1.75-inch board, verify all 27
LED positions, upward motion on both arms, gapless 400 ms preview wrapping, the
750 ms automatic transition, immediate short-press restoration, and mode order.
