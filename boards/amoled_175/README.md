# Waveshare ESP32-S3 Touch AMOLED 1.75

## Profile

This profile targets the standard Waveshare ESP32-S3-Touch-AMOLED-1.75 board.
The `-B` enclosure variant uses the same base hardware. The `-G` variant adds an
LC76G GNSS module and shares GPIO17, GPIO18, and TCA9554 P7; it requires an
explicit compatibility review before those resources are used.

| Item | Configuration |
|:-----|:--------------|
| MCU | ESP32-S3R8, dual-core Xtensa LX7 up to 240 MHz |
| Memory | 8 MB octal PSRAM, 16 MB external flash |
| Display | 1.75-inch 466 x 466 CO5300 QSPI AMOLED |
| Touch | CST9217 capacitive touch, I2C address `0x5A` |
| Storage | microSD, native 1-bit SDMMC |
| Power | AXP2101 PMIC, I2C address `0x34` |
| Motion | QMI8658 6-axis IMU, I2C address `0x6B` |
| RTC | PCF85063, I2C address `0x51` |
| Audio | ES8311 output codec, ES7210 microphone ADC, NS4150B amplifier |
| I/O expansion | TCA9554, I2C address `0x20` |
| BSP | `waveshare/esp32_s3_touch_amoled_1_75` version `3.0.1` |

## GPIO Allocation

The table includes every ESP32-S3 GPIO routed to a named board signal or the
expansion header by the official hardware reference. Internal flash/PSRAM pins
and unrouted pins are intentionally omitted.

**Direction:** `In` = input, `Out` = output, `I/O` = bidirectional.  
**Source:** `[HW]` = [official hardware reference][hw]; `[USER]` = explicit
project wiring supplied for the external light ring and button.

### System, Storage, and USB

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:-----|:----------------|:------------------|:----|:----------|:----------------|:-------|
| 0 | `BOOT` | Boot button | Strap | In | Low at reset enters download mode | [HW] |
| 1 | `SD_CMD` | microSD | SDMMC | I/O | Reserved while mounted | [HW] |
| 2 | `SD_CLK` | microSD | SDMMC | Out | Reserved while mounted | [HW] |
| 3 | `SD_D0` | microSD | SDMMC | I/O | 1-bit data line | [HW] |
| 19 | `USB_D-` | USB-C | USB | I/O | Reserved by native USB | [HW] |
| 20 | `USB_D+` | USB-C | USB | I/O | Reserved by native USB | [HW] |
| 41 | `SD_D3/CS` | microSD | SDMMC/SPI | I/O | Wired; unused in BSP 1-bit mode | [HW] |

### Display and Touch

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:-----|:----------------|:------------------|:----|:----------|:----------------|:-------|
| 4 | `LCD_DATA0` | CO5300 AMOLED | QSPI | I/O | Display bus | [HW] |
| 5 | `LCD_DATA1` | CO5300 AMOLED | QSPI | I/O | Display bus | [HW] |
| 6 | `LCD_DATA2` | CO5300 AMOLED | QSPI | I/O | Display bus | [HW] |
| 7 | `LCD_DATA3` | CO5300 AMOLED | QSPI | I/O | Display bus | [HW] |
| 11 | `TP_INT` | CST9217 touch | GPIO IRQ | In | Touch interrupt | [HW] |
| 12 | `LCD_CS` | CO5300 AMOLED | QSPI | Out | Chip select | [HW] |
| 13 | `LCD_TE` | CO5300 AMOLED | GPIO sync | In | Tearing-effect sync | [HW] |
| 38 | `LCD_PCLK` | CO5300 AMOLED | QSPI | Out | Display clock | [HW] |
| 39 | `LCD_RST` | CO5300 AMOLED | GPIO | Out | Separate display reset | [HW] |
| 40 | `TP_RST` | CST9217 touch | GPIO | Out | Separate touch reset | [HW] |

### Shared I2C and IMU

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:-----|:----------------|:------------------|:----|:----------|:----------------|:-------|
| 14 | `I2C_SCL` | All onboard I2C devices | I2C | I/O | Shared; BSP default 400 kHz | [HW] |
| 15 | `I2C_SDA` | All onboard I2C devices | I2C | I/O | Shared; check address conflicts | [HW] |
| 21 | `QMI_INT2` | QMI8658 IMU | GPIO IRQ | In | INT1 uses TCA9554 P6 | [HW] |

### Audio and Expansion Header

| GPIO | Signal/Function | Peripheral/Sensor | Bus | Direction | Shared/Conflict | Source |
|:-----|:----------------|:------------------|:----|:----------|:----------------|:-------|
| 8 | `I2S_DOUT` | ES8311 codec | I2S | Out | Playback data | [HW] |
| 9 | `I2S_BCLK/SCLK` | ES8311 + ES7210 | I2S | Out | Shared TX/RX clock | [HW] |
| 10 | `I2S_DIN/ASDOUT` | ES7210 ADC | I2S/TDM | In | Four-slot capture | [HW] |
| 16 | `LIGHTRING_DATA` | External WS2812 ring | RMT | Out | Used by `lightring_button` | [USER] |
| 17 | `GPIO17` | Header pin 6 | GPIO/UART | I/O | `-G`: LC76G RX route | [HW] |
| 18 | `BUTTON` | External button | GPIO | In | Active low; `-G`: LC76G TX conflict | [USER] |
| 42 | `I2S_MCLK` | ES8311 + ES7210 | I2S | Out | Shared master clock | [HW] |
| 43 | `U0TXD` | Header pin 5 | UART0 | Out | May carry console output | [HW] |
| 44 | `U0RXD` | Header pin 4 | UART0 | In | May be used by console | [HW] |
| 45 | `I2S_LRCK/WS` | ES8311 + ES7210 | I2S | Out | Shared TX/RX frame clock | [HW] |
| 46 | `PA_CTRL` | NS4150B amplifier | GPIO | Out | BSP owns active level | [HW] |

## Sensors and Peripherals

### Display and Touch

| Device | Interface | Control | GPIO | Role |
|:-------|:----------|:--------|:-----|:-----|
| CO5300 display | QSPI, CS 12 | TE 13, RST 39 | 4-7, 12, 13, 38, 39 | AMOLED display |
| CST9217 touch | I2C `0x5A` | INT 11, RST 40 | 11, 14, 15, 40 | Two-point touch |

The display uses panel commands for brightness and has no GPIO backlight.

### Power, Motion, and System

| Device | Interface | Control | GPIO | Role |
|:-------|:----------|:--------|:-----|:-----|
| AXP2101 PMIC | I2C `0x34` | TCA9554 P5 IRQ | 14, 15 | Power and telemetry |
| QMI8658 IMU | I2C `0x6B` | INT2 21; INT1 P6 | 14, 15, 21 | 6-axis motion |
| PCF85063 RTC | I2C `0x51` | TCA9554 P3 IRQ | 14, 15 | Battery-backed RTC |
| TCA9554 expander | I2C `0x20` | N/A | 14, 15 | System signal routing |

The QMI8658 dependency is declared separately because the BSP exposes no IMU
API. The TCA9554 also routes power, PMU, IMU, RTC, and optional GNSS signals.

### Audio, Storage, and Optional GNSS

| Device | Interface | Control | GPIO | Role |
|:-------|:----------|:--------|:-----|:-----|
| ES8311 codec | I2C `0x18`, I2S | TCA9554 enable | 8, 9, 14, 15, 42, 45, 46 | Speaker output |
| ES7210 ADC | I2C `0x40`, I2S/TDM | N/A | 9, 10, 14, 15, 42, 45 | Microphone input |
| microSD | 1-bit SDMMC | No CD/WP | 1, 2, 3, 41 | Removable storage |
| LC76G (`-G` only) | I2C `0x50`/`0x54` | TCA9554 P7 RST | 14, 15, 17, 18 | Optional GNSS |
| WS2812 ring | RMT | 27 LEDs, GRB wire order | 16 | External status lighting |
| Button | GPIO | Active-low input with pull-up | 18 | External light-ring control |

The ES7210 captures two onboard microphones and the playback reference. GPIO41
is wired to microSD but unused by the current mount code. LC76G is absent from
the standard and `-B` variants.

## Project Compatibility

The source of truth is [supported_apps.txt](supported_apps.txt).

| App | Status | Reason |
|:----|:-------|:-------|
| `slide_player` | Supported | Uses the BSP display, touch, and SD APIs without a board-specific display config |
| `salary_cat` | Supported | Uses BSP display, touch, SD, ES8311 audio, TCA9554 initialization, and AXP2101 PMU APIs |
| `acc_data` | Supported | Uses the common BSP display API and active LVGL resolution; haptic output is disabled, so GPIO18 remains available to expansion/GNSS |
| `battery_monitor` | Incompatible | Current app uses the 2.06-specific display config structure |
| `lightring_button` | Supported | Uses external WS2812 data on GPIO16 and an active-low button on GPIO18; no display or LVGL |

These are project integration limits, not claims that the physical board lacks
the PMU, IMU, or audio devices.

## Build Configuration

- Flash size: 16 MB.
- Partition table: 8 MB factory app and 7 MB SPIFFS storage.
- PSRAM: octal mode at 80 MHz from the common sdkconfig defaults.
- Salary Cat enables the LVGL GIF decoder through its app sdkconfig defaults.
- Light Ring Button uses the app-owned Espressif RMT LED strip driver and does
  not initialize the display.
- The display keeps the BSP communication frequency and uses the BSP's 50-row
  PSRAM double-buffer profile; this project does not override the QSPI clock.
- Salary Cat initializes the BSP-owned TCA9554 before creating the ES8311 codec.
  The official BSP continues to own GPIO46 PA control; no undocumented TCA9554
  output bit is inferred for codec or amplifier enable.
- The standard, `-B`, and `-G` variants must not share external GPIO assumptions
  without checking the variant-specific routing.

## References

- [Official product repository][repo]
- [Official hardware reference and schematic cross-check][hw]
- [Maintained BSP header][bsp]
- [ESP Component Registry BSP package][registry]

[repo]: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75
[hw]: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md
[bsp]: https://github.com/waveshareteam/Waveshare-ESP32-components/blob/master/bsp/esp32_s3_touch_amoled_1_75/include/bsp/esp32_s3_touch_amoled_1_75.h
[registry]: https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75