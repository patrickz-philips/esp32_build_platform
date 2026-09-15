#pragma once

// Waveshare ESP32-S3 Touch AMOLED 1.75" board profile.
#define BOARD_NAME              "Waveshare ESP32-S3 Touch AMOLED 1.75\""

// Physical board capabilities. App compatibility lives in supported_apps.txt.
#define BOARD_HAS_SD            1
#define BOARD_HAS_TOUCH         1
#define BOARD_HAS_AUDIO         1
#define BOARD_HAS_IMU           1
#define BOARD_HAS_PMU           1
#define BOARD_HAS_RTC           1
#define BOARD_HAS_HAPTIC        0
#define BOARD_HAS_LIGHTRING_BUTTON 1

#define BOARD_LIGHTRING_GPIO     16
#define BOARD_BUTTON_GPIO        18

// Current project integration status, not raw hardware capability.
#define BOARD_FEATURE_SLIDE_PLAYER 1
#define BOARD_FEATURE_SALARY_CAT   1
#define BOARD_FEATURE_ACC_DATA     1
#define BOARD_FEATURE_LIGHTRING_BUTTON 1

// SD card is mounted via the board BSP (bsp_sdcard_mount); wiring lives there.
