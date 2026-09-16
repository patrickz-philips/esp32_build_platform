# Spotpear ESP32-C6-LCD-0.96

## Profile

This project-owned BSP targets the Spotpear ESP32-C6-LCD-0.96 board. It uses
the maintained `waveshare/esp_lcd_st7735` 2.0.x component and
`espressif/esp_lvgl_port` 2.9.x. The LCD and micro-SD slot share SPI2.

| Item | Project Configuration |
|:---|:---|
| SoC | ESP32-C6FH4, single-core RISC-V up to 160 MHz |
| Memory | 4 MB embedded flash; no PSRAM |
| Display | ST7735, 0.96-inch 160 x 80 RGB565 SPI |
| Input | BOOT button on GPIO9, active low |
| Storage | micro-SD over shared SPI2 |
| Touch | None |
| Audio / sensors / PMU | None |
| BSP | Project-owned ST7735 + SD + LVGL port adapter |

## Sources

- `[SCH]`: [Spotpear schematic at source commit 971a504][sch]
- `[DEMO]`: [Spotpear Arduino SD/display demo at source commit 971a504][demo]
- `[REG]`: [Waveshare ST7735 2.0.0 component registry entry][registry]
- `[ESP]`: [Espressif ESP32-C6 datasheet][esp]
- `[USER]`: GPIO9 slide-player behavior supplied with this board-integration request

[sch]: https://github.com/spotpear37/ESP32-C6-0.96/blob/971a5048d047d471e38127cd2d2933460ca5d30e/0.96/ESP32-C6-0.96-LCD.pdf
[demo]: https://github.com/spotpear37/ESP32-C6-0.96/tree/971a5048d047d471e38127cd2d2933460ca5d30e/Arduino/sd_demo/Video_demo
[registry]: https://components.espressif.com/components/waveshare/esp_lcd_st7735/versions/2.0.0
[esp]: https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf

## GPIO Allocation

### Display and Shared SPI

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:---|:---|:---|:---|:---|:---|:---|
| 3 | `LCD_BL` | ST7735 backlight | LEDC PWM | Out | N/A | [SCH], [DEMO] |
| 5 | `SPI_MISO` | micro-SD | SPI2 | In | LCD bus; strapping pin; LCD is write-only | [SCH], [DEMO], [ESP] |
| 6 | `SPI_MOSI` | ST7735 + micro-SD | SPI2 | Out | Shared LCD `SDA` / SD `MOSI` | [SCH], [DEMO] |
| 7 | `SPI_SCLK` | ST7735 + micro-SD | SPI2 | Out | Shared clock | [SCH], [DEMO] |
| 14 | `LCD_CS` | ST7735 display | SPI2 | Out | LCD chip select | [SCH], [DEMO] |
| 15 | `LCD_DC` | ST7735 display | GPIO/strap | Out | Data/command select; strapping pin | [SCH], [DEMO], [ESP] |
| 21 | `LCD_RST` | ST7735 display | GPIO | Out | Display reset | [SCH], [DEMO] |

### Storage

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:---|:---|:---|:---|:---|:---|:---|
| 4 | `SD_CS` | micro-SD slot | SPI2 | Out | 10 kOhm pull-up; strapping pin | [SCH], [DEMO], [ESP] |
| 5 | `SD_MISO` | micro-SD slot | SPI2 | In | Shared with LCD bus | [SCH], [DEMO] |
| 6 | `SD_MOSI` | micro-SD slot | SPI2 | Out | Shared with LCD bus | [SCH], [DEMO] |
| 7 | `SD_CLK` | micro-SD slot | SPI2 | Out | Shared with LCD bus | [SCH], [DEMO] |

### System and USB

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:---|:---|:---|:---|:---|:---|:---|
| 9 | `BOOT` / next slide | Push button | GPIO/strap | In | 10 kOhm pull-up; active low; strapping pin | [SCH], [DEMO], [USER] |
| 12 | `USB_D-` | USB Type-C | USB | I/O | Native USB Serial/JTAG | [SCH], [ESP] |
| 13 | `USB_D+` | USB Type-C | USB | I/O | Native USB Serial/JTAG | [SCH], [ESP] |
| 16 | `U0TXD` | UART0 / header P8 | UART | Out | 499 Ohm series resistor; exposed header | [SCH] |
| 17 | `U0RXD` | UART0 / header P5 | UART | In | Exposed header | [SCH] |

### Expansion Headers

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:---|:---|:---|:---|:---|:---|:---|
| 0 | `EXP_GPIO0` | Header P9 | GPIO | I/O | Exposed only | [SCH] |
| 1 | `EXP_GPIO1` | Header P7 | GPIO | I/O | Exposed only | [SCH] |
| 2 | `EXP_GPIO2` | Header P3 | GPIO | I/O | Exposed only | [SCH] |
| 8 | `EXP_GPIO8` | Header P4 | GPIO/strap | I/O | Strapping pin | [SCH], [ESP] |
| 18 | `EXP_GPIO18` | Header P1 | GPIO | I/O | Exposed only | [SCH] |
| 19 | `EXP_GPIO19` | Header P2 | GPIO | I/O | Exposed only | [SCH] |
| 20 | `EXP_GPIO20` | Header P6 | GPIO | I/O | Exposed only | [SCH] |

## On-Board Peripherals

### Display and Storage

| Device | Interface | Control | GPIO | Role |
|:---|:---|:---|:---|:---|
| ST7735 LCD | SPI2 | CS 14, DC 15, RST 21, BL 3 | 3, 5-7, 14, 15, 21 | 160 x 80 display |
| micro-SD | SPI2 | CS 4 | 4-7 | PNG/GIF storage |

The vendor demo uses a `(1, 26)` GRAM offset and `0xA8` MADCTL value for its
160 x 80 landscape mode. This BSP uses the corresponding `(26, 1)` offset and
`0x08` MADCTL value for a native 80 x 160 portrait viewport. Its refresh timer
runs every 50 ms (20 FPS). SPI starts at 40 MHz for conservative bring-up; the
Arduino demo requests 80 MHz.

The LCD and micro-SD share SPI2. The display uses one draw buffer so image
decoding cannot start another SD transaction while an LCD DMA transfer is still
running. Background SD probes also drain the LCD queue before accessing files.

### Input and Connectivity

| Device | Interface | Control | GPIO | Role |
|:---|:---|:---|:---|:---|
| BOOT button | GPIO | Active low | 9 | Advance and wrap slides |
| USB Type-C | Native USB | D-/D+ | 12, 13 | Power and USB Serial/JTAG |
| UART0 | UART | TX/RX | 16, 17 | Serial console / headers |

## App Compatibility

| App | Status | Reason |
|:---|:---|:---|
| `slide_player` | Supported | Uses the LCD, micro-SD, and GPIO9 button |
| `salary_cat` | Incompatible | Requires audio hardware and an AXP2101 PMU |
| `acc_data` | Incompatible | Requires a QMI8658 IMU |
| `battery_monitor` | Incompatible | Requires an AXP2101 PMU |
| `lightring_button` | Incompatible | Requires an on-board WS2812 light ring |

On this non-touch board, a debounced GPIO9 press advances to the next numbered
PNG/GIF slide. Pressing on the highest numbered slide wraps to slide 1. If the
requested PNG and GIF are both unavailable, the same GPIO9 request immediately
falls back to slide 1. Touch-capable boards keep their existing left/right
gesture behavior.

The primary console uses the native USB Serial/JTAG port at 115200 baud and
accepts `next`, `last`, or a slide number up to the highest numbered SD asset.

## Build Configuration

| Setting | Value |
|:---|:---|
| Target | `esp32c6` |
| Flash | 4 MB QIO |
| PSRAM | Disabled |
| Partition table | NVS 24 KB, PHY 4 KB, factory app 3 MB |
| LVGL draw buffers | One DMA-capable 80 x 20 RGB565 stripe |
| LCD driver | `waveshare/esp_lcd_st7735 ^2.0.0` |
| LVGL integration | `espressif/esp_lvgl_port ^2.9.0` |

## Device Checks

The build cannot verify physical behavior. Confirm display orientation, GRAM
offset, RGB/BGR order, inversion, backlight polarity, SD stability while the LCD
is refreshing, GPIO9 debounce, PNG/GIF rendering, GIF animation memory, and
highest-slide-to-1 wrap-around on the board.