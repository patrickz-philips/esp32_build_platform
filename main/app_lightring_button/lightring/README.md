# ESP32-S3 Light Ring Component

This is a self-contained 27-LED light-ring component. Its public API
accepts light-domain events rather than button events, so GPIO, BLE, a UI, or
another controller can all drive the same state machine.

## Public API

```c
int lightring_activate(lightring_mode_t mode);
int lightring_deactivate(void);
int lightring_begin_mode_change(lightring_mode_t next_mode);
int lightring_cancel_mode_change(void);
int lightring_commit_mode_change(void);
```

Calls return `0` when accepted and `-1` when the event is invalid in the
current state, the mode is invalid, or the component is not initialized.

Modes are explicit rather than numeric variants:

| Mode | Palette |
|------|---------|
| `LIGHTRING_MODE_INTENSE` | Intense (#4) |
| `LIGHTRING_MODE_SENSITIVE` | Sensitive (#5) |
| `LIGHTRING_MODE_REGULAR` | Regular (#6) |

## State machine

| Event | Valid state | Effect | Final state |
|-------|-------------|--------|-------------|
| `lightring_activate(mode)` | `OFF` | Mode color expands from LEDs 26/27 in 350 ms | `ON` |
| `lightring_deactivate()` | `ON` | Color collapses toward LEDs 26/27 and turns fully off in 350 ms | `OFF` |
| `lightring_begin_mode_change(next)` | `ON` | Collapses to LEDs 26/27 in 300 ms, waits there until 750 ms, then blends the edge to `next` in 200 ms | `MODE_PREVIEW` |
| `lightring_cancel_mode_change()` | Before 750 ms | LEDs 26/27 breathe out in 200 ms | `OFF` |
| `lightring_commit_mode_change()` | At or after 750 ms | The new mode color expands from LEDs 26/27 in 400 ms | `ON` |

LED labels in this table are one-based. They are physical indices 25 and 26
in the source.

The component measures the 750 ms decision boundary from
`lightring_begin_mode_change()`. It does not know about button press or release
events. A button adapter can map its own events to the domain API:

```c
/* Application code, not part of lightring. */
void on_control_down(void)
{
    if (lightring_get_status() == LIGHTRING_STATUS_OFF) {
        (void) lightring_activate(selected_mode);
    } else if (lightring_get_status() == LIGHTRING_STATUS_ON) {
        interaction_started = xTaskGetTickCount();
        (void) lightring_begin_mode_change(next_mode);
    }
}

void on_control_up(void)
{
    uint32_t held_ms = ticks_to_ms(xTaskGetTickCount() - interaction_started);
    if (held_ms < 750U) {
        (void) lightring_cancel_mode_change();
    } else {
        (void) lightring_commit_mode_change();
    }
}
```

## Integration

The ESP-IDF component compiles the renderer, palettes, orchestrator, and
`ws2812_esp.c`. The output backend uses the ESP32-S3 RMT TX driver with explicit
WS2812 timing. Initialize after the scheduler is available:

```c
lightring_config_t config = {0};
config.ws2812.gpio_num = 16;
config.ws2812.led_count = LIGHTRING_LED_COUNT;

if (lightring_init(&config) == 0) {
    (void) lightring_activate(LIGHTRING_MODE_REGULAR);
}
```

The renderer frame buffer remains RGB. The RMT output boundary remaps each pixel
to the WS2812 GRB wire order without changing renderer or palette data.

## Host test

```sh
cc -std=c11 -Wall -Wextra -Werror -I include \
  test/test_effects_host.c src/lightring_palette.c src/lightring_effects.c \
    -lm -o /tmp/lightring_test && /tmp/lightring_test
```

The test checks full expansion, the LED 26/27 hold frame, complete collapse,
the 200 ms edge fade, and the endpoint of the edge color blend.