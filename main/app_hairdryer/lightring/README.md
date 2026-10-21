# ESP32-S3 and STM32F429 Light Ring Component

This self-contained component reproduces the six thermo/warm/hot high/low
24-LED effects in the parent `export` directory. The logical layout is an
8-LED inner ring plus a 16-LED outer ring, mapped to the physical WS2812 slots
stored in `Nairobi Device.ledsetting.json`.

- THERMO uses the `thermo-*-blend-rotation.effect.json` files: a 4000 ms
  rotation with linearly blended colors between each LED's 500 ms keyframes.
  Low speed lights only the inner ring at brightness 255; high speed uses
  brightness 80 for the inner ring and 200 for the outer ring.
- HOT, WARM, and the independent cold effect retain their 2500 ms breathing
  animation and existing palettes.

Both THERMO speed variants have the same rotation period, matching the JSON.
The shared phase wraps at 20000 ms (a common multiple of both periods), so
neither effect jumps at the other's loop boundary.

## Public API

```c
int lightring_init(const lightring_config_t *config);
int lightring_change_mode(lightring_mode_t mode);
int lightring_change_speed(lightring_speed_t speed);
int lightring_cold_on(void);
int lightring_cold_off(void);
```

Modes are `LIGHTRING_MODE_THERMO`, `LIGHTRING_MODE_HOT`, and
`LIGHTRING_MODE_WARM`. Output states are selected through the speed API:
`LIGHTRING_SPEED_OFF`, `LIGHTRING_SPEED_LOW`, and `LIGHTRING_SPEED_HIGH`.
`OFF` is the fully extinguished state.

Calls return `0` when accepted and `-1` for invalid input, an uninitialized
component, a redundant request, or while another transition is active.

- `change_mode` blends to the new palette in 300 ms without resetting phase.
- `high -> low` fades the outer ring into the inner ring in 300 ms.
- `low -> high` blends immediately in 300 ms for THERMO, preserving rotation
  phase. Breathing effects still wait until the low-speed inner ring reaches
  maximum brightness, then blend to the high-speed inner-lit frame in 300 ms.
- `low/high -> off` switches to the low-speed base frame and breathes out to
  black in 300 ms.
- `off -> low/high` breathes in to the requested speed in 300 ms.
- `cold_on/off` blends the current effect to/from the independent blue cold
  palette in 300 ms. Releasing during `cold_on` reverses smoothly from the
  current blend instead of dropping the release.

THERMO rotation phase keeps advancing during speed and on/off transitions.
Cold retains its breathing brightness even when the selected mode is THERMO.

The selected HOT/WARM/THERMO mode is retained while cold is enabled, so
`cold_off` always returns to that mode. Query current state with `lightring_get_status()`,
`lightring_get_mode()`, `lightring_get_speed()`, and
`lightring_is_cold()`. An asynchronous output failure is reported as
`LIGHTRING_STATUS_ERROR`, and later transition requests are rejected.

Colors and brightness are baked through the enabled correction pipeline from
`Nairobi Device.ledsetting.json`: sRGB decode, fitted CCM, brightness inverse
LUT, channel inverse LUTs, and GRB wire order. No runtime calibration fitting
is required.

## Integration

### ESP32-S3

`CMakeLists.txt` selects the RMT backend:

```c
lightring_config_t config = {0};
config.ws2812.gpio_num = 16;
config.ws2812.led_count = LIGHTRING_LED_COUNT;
config.initial_mode = LIGHTRING_MODE_THERMO;
config.initial_speed = LIGHTRING_SPEED_HIGH;

if (lightring_init(&config) == 0) {
    (void)lightring_change_mode(LIGHTRING_MODE_WARM);
}
```

### STM32F429

Compile `ws2812.c` plus either or both DMA backends:

```c
lightring_config_t config = {0};
config.ws2812.backend = LIGHTRING_WS2812_SPI_DMA;
config.ws2812.spi = &hspi2;
config.ws2812.led_count = LIGHTRING_LED_COUNT;
config.initial_mode = LIGHTRING_MODE_THERMO;
config.initial_speed = LIGHTRING_SPEED_LOW;
config.task_stack_words = 512U;

(void)lightring_init(&config);
```

## Host test

```sh
cc -std=c11 -Wall -Wextra -Werror -I include \
  test/test_effects_host.c src/lightring_effects.c \
  -lm -o /tmp/lightring_test && /tmp/lightring_test
```

The test checks Nairobi-calibrated GRB bytes, the JSON-derived physical
mapping, rotation loop closure, high/low/off endpoints, the cold palette,
outer-ring collapse, and the low-to-high landing frame.

From this component directory, run the full JSON parity and transition check:

```sh
node test/test_json_parity.mjs
```

This compiles both the renderer and the controller (with host FreeRTOS stubs),
compares all 24 calibrated pixels against the eight effect JSON files every
10 ms through the shared 20000 ms cycle, and checks THERMO phase continuity,
immediate low-to-high transitions, on/off transitions, and cold's wait-for-peak
behavior. Temporary build files are removed automatically.
