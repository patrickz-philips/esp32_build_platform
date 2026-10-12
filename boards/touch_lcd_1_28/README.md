# Waveshare ESP32-S3-Touch-LCD-1.28

## Profile

This board owns its BSP instead of consuming a vendor package: there is no
`waveshare/esp32_s3_touch_lcd_1_28` component on the ESP Component Registry. The
BSP under [bsp/](bsp/) drives a GC9A01 240x240 round SPI panel and a CST816S I2C
touch controller through `espressif/esp_lvgl_port`, exposing the same
`bsp_display_start` / `bsp_display_lock` / `bsp_i2c_get_handle` API the apps use.

| Item | Project Configuration |
|:-----|:----------------------|
| MCU | ESP32-S3R2, dual-core Xtensa LX7 up to 240 MHz |
| Memory | 16 MB flash (QIO), 2 MB Quad PSRAM |
| Display | GC9A01A, 1.28-inch 240 x 240 round RGB565 SPI, 4-wire |
| Touch | CST816S capacitive, I2C address `0x15` |
| Motion | QMI8658 6-axis IMU, I2C address `0x6B` |
| Power | ETA6096 dumb Li charger (no I2C PMU); battery sense on ADC GPIO1 |
| Storage | None (no micro-SD slot) |
| Audio | None |
| BSP | Project-owned (`esp_lcd_gc9a01` + `esp_lcd_touch_cst816s` + `esp_lvgl_port`) |

## GPIO Allocation

**Direction:** `In` = input, `Out` = output, `I/O` = bidirectional.
**Source:** `[HW]` = [official Waveshare pin table / wiki][hw]; `[BSP]` = this
board's BSP ([board_config.h](board_config.h), [bsp/](bsp/)).

[hw]: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28

### Display and Backlight (SPI)

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:-----|:----------------|:------------------|:----|:----------|:----------------|:-------|
| 2 | `LCD_BL` | GC9A01 backlight | LEDC PWM | Out | N/A | [BSP] |
| 8 | `LCD_DC` | GC9A01 display | SPI | Out | Data/command select | [BSP] |
| 9 | `LCD_CS` | GC9A01 display | SPI | Out | Chip select | [BSP] |
| 10 | `LCD_CLK` | GC9A01 display | SPI | Out | SPI2 SCLK | [BSP] |
| 11 | `LCD_MOSI` | GC9A01 display | SPI | Out | SPI2 MOSI | [BSP] |
| 12 | `LCD_MISO` | GC9A01 display | SPI | I/O | Wired; unused (write-only panel) | [HW] |
| 14 | `LCD_RST` | GC9A01 display | GPIO | Out | Display reset | [BSP] |

### Touch and Shared I2C

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:-----|:----------------|:------------------|:----|:----------|:----------------|:-------|
| 5 | `TP_INT` | CST816S touch | GPIO IRQ | In | Shared with `MOSFET2_CS` | [HW] |
| 6 | `I2C_SDA` | Touch + IMU | I2C | I/O | Shared bus (touch `0x15`, IMU `0x6B`) | [BSP] |
| 7 | `I2C_SCL` | Touch + IMU | I2C | I/O | Shared bus | [BSP] |
| 13 | `TP_RST` | CST816S touch | GPIO | Out | Touch reset | [BSP] |

### IMU

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:-----|:----------------|:------------------|:----|:----------|:----------------|:-------|
| 3 | `IMU_INT2` | QMI8658 | GPIO IRQ | In | Not wired to firmware (polled) | [HW] |
| 4 | `IMU_INT1` | QMI8658 | GPIO IRQ | In | Shared with `MOSFET1_CS`; polled | [HW] |

### System, Power, and Expansion

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:-----|:----------------|:------------------|:----|:----------|:----------------|:-------|
| 0 | `BOOT` | Boot button | GPIO/strap | In | Boot strapping pin | [HW] |
| 1 | `BAT_ADC` | Battery divider | ADC1 | In | 200K/100K divider; unused by firmware | [HW] |
| 15 | `EXP_IO15` | SH1.0 header | GPIO | I/O | Expansion connector | [HW] |
| 16 | `EXP_IO16` | SH1.0 header | GPIO | I/O | Expansion connector | [HW] |
| 17 | `EXP_IO17` | SH1.0 header | GPIO | I/O | Expansion connector | [HW] |
| 18 | `EXP_IO18` | SH1.0 header | GPIO | I/O | Expansion connector | [HW] |
| 21 | `EXP_IO21` | SH1.0 header | GPIO | I/O | Expansion connector | [HW] |
| 33 | `EXP_IO33` | SH1.0 header | GPIO | I/O | Expansion connector | [HW] |
| 43 | `UART0_TX` | ESP32-S3 UART0 | UART | Out | Console/programming | [HW] |
| 44 | `UART0_RX` | ESP32-S3 UART0 | UART | In | Console/programming | [HW] |

`MOSFET1_CS` (GPIO4) and `MOSFET2_CS` (GPIO5) are the board's two low-side
MOSFET control contacts. They physically share pins with the QMI8658 `INT1` and
the touch `INT` lines respectively. This BSP uses GPIO5 as the touch interrupt
and leaves GPIO4 unused, so the MOSFET contacts are not driven by firmware.

## Sensors and Peripherals

### Display and Touch

| Device | Interface | Control | GPIO | Role |
|:-------|:----------|:--------|:-----|:-----|
| GC9A01A display | SPI2, CS 9 | DC 8, RST 14, BL 2 | 2, 8-12, 14 | BSP display controller |
| CST816S touch | I2C `0x15` | INT 5, RST 13 | 5, 6, 7, 13 | BSP touch controller |

The CST816S only ACKs I2C reads shortly after it raises `INT`; do not expect a
response on init. It reports a single point plus a gesture code.

### Motion and Power

| Device | Interface | Control | GPIO | Role |
|:-------|:----------|:--------|:-----|:-----|
| QMI8658 IMU | I2C `0x6B` | INT1 4, INT2 3 | 3, 4, 6, 7 | 6-axis motion (polled) |
| ETA6096 charger | N/A | N/A | 1 (ADC) | Dumb Li charger; no I2C registers |

The ETA6096 is a fixed-function charger with no host interface. Battery voltage
is only observable through the ADC divider on GPIO1
(`Vbat = 3.3 / 4096 * 3 * ADraw`). Firmware currently leaves this unread; see the
compatibility notes below.

## Project Compatibility

The source of truth is [supported_apps.txt](supported_apps.txt). `acc_data`
runs IMU-only; `hairdryer` uses only expansion GPIOs.

| App | Status | Reason |
|:----|:-------|:-------|
| `acc_data` | Supported | Uses the QMI8658 IMU + GPIO0 button; PMU and SD paths are compiled out |
| `hairdryer` | Supported | Holds WS2812 data GPIO15 low and samples active-low buttons on GPIO33, GPIO21, GPIO18, and GPIO17 |
| `slide_player` | Incompatible | Requires micro-SD storage (`#error` without `BOARD_HAS_SD`) |
| `salary_cat` | Incompatible | Requires micro-SD + ES8311 audio + AXP2101 PMU |
| `battery_monitor` | Incompatible | Requires the AXP2101 PMU register interface |

`acc_data` normally logs accelerometer CSV files to SD and reads battery state
from an AXP2101 PMU. This board has neither, so the app is built with
`BOARD_HAS_SD = 0` and `BOARD_HAS_PMU = 0`:

- The SD writer (`sd_acc_writer.c`) is excluded from the build; the accelerometer
  stream is shown live on the LCD but not recorded.
- The PMU task, power-button handling, and shutdown are guarded off; the battery
  label stays at `0%`.

`hairdryer` debounces its mode, cold, and three-position switch inputs and sends
named events to its light-ring dispatcher. Its light effect is intentionally not
implemented; GPIO15 is configured as a low WS2812 data output until an effect is
defined.

## Build Configuration and Limits

| Setting | Value |
|:--------|:------|
| Flash | 16 MB, QIO |
| PSRAM | 2 MB, Quad mode (overrides the AMOLED Octal default) |
| Partition table | [partitions.csv](partitions.csv): `nvs` + `phy_init` + 4 MB `factory` |
| LVGL draw buffer | Two DMA-capable 240 x 40 stripes in internal RAM |
| SPI clock | 40 MHz (GC9A01 supports up to 80 MHz) |

Known limits and unverified items:

- Display color order (`BGR` + inversion), orientation, and touch axis mapping
  are set to the common GC9A01/CST816S defaults but have **not** been verified on
  hardware; adjust `esp_lcd_panel_*` and touch `flags` if the image or touch is
  mirrored/inverted.
- No on-device smoke test has been run. SPI clock, PSRAM Quad timing, and
  backlight PWM need bring-up confirmation.
- Battery voltage (ADC GPIO1) and the two MOSFET contacts are not driven by
  firmware.
