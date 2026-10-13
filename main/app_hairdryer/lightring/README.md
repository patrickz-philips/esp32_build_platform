# ESP32-S3 and STM32F429 Light Ring Component

This self-contained component reproduces the six thermo/warm/hot high/low
24-LED effects in the parent `export` directory. The logical layout is an
8-LED inner ring plus a 16-LED outer ring, mapped to the physical WS2812 slots
stored in the JSON files. The 2500 ms animation phase keeps running through
every transition.

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
- `low -> high` waits until the low-speed inner ring reaches maximum
  brightness, then blends to the high-speed inner-lit frame in 300 ms.
- `low/high -> off` switches to the low-speed base frame and breathes out to
  black in 300 ms.
- `off -> low/high` breathes in to the requested speed in 300 ms.
- `cold_on/off` blends the current effect to/from the independent blue cold
  palette in 300 ms. Releasing during `cold_on` reverses smoothly from the
  current blend instead of dropping the release.

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
mapping, high/low/off endpoints, the cold palette, outer-ring collapse, and the
low-to-high landing frame.
