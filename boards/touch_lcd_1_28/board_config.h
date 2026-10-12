#pragma once

// Waveshare ESP32-S3-Touch-LCD-1.28 board profile.
#define BOARD_NAME              "Waveshare ESP32-S3-Touch-LCD-1.28"

// Physical board capabilities. App compatibility lives in supported_apps.txt.
// This board has no micro-SD slot and no I2C PMU (ETA6096 dumb Li charger).
#define BOARD_HAS_SD            0
#define BOARD_HAS_TOUCH         1
#define BOARD_HAS_AUDIO         0
#define BOARD_HAS_IMU           1
#define BOARD_HAS_PMU           0
#define BOARD_HAS_RTC           0
#define BOARD_HAS_HAPTIC        0

// Current project integration status, not raw hardware capability.
#define BOARD_FEATURE_ACC_DATA     1
#define BOARD_FEATURE_HAIRDRYER    1

// On-board GPIO map. Verified from the official Waveshare demo DEV_Config.h and
// Setup207_GC9A01.h; do not change without an authoritative source.
#define BOARD_I2C_SDA_GPIO      6
#define BOARD_I2C_SCL_GPIO      7
#define BOARD_LCD_SCLK_GPIO     10
#define BOARD_LCD_MOSI_GPIO     11
#define BOARD_LCD_DC_GPIO       8
#define BOARD_LCD_CS_GPIO       9
#define BOARD_LCD_RST_GPIO      14
#define BOARD_LCD_BL_GPIO       2
#define BOARD_TOUCH_INT_GPIO    5
#define BOARD_TOUCH_RST_GPIO    13
#define BOARD_HAIRDRYER_WS2812_GPIO 15
#define BOARD_HAIRDRYER_BUTTON_1_GPIO 33
#define BOARD_HAIRDRYER_BUTTON_2_GPIO 21
#define BOARD_HAIRDRYER_BUTTON_3_GPIO 18
#define BOARD_HAIRDRYER_BUTTON_4_GPIO 17
#define BOARD_LCD_H_RES         240
#define BOARD_LCD_V_RES         240
