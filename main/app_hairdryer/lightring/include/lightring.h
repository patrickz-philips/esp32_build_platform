#pragma once

#include <stdint.h>

#include "ws2812.h"

typedef enum {
    LIGHTRING_MODE_THERMO = 0,
    LIGHTRING_MODE_HOT,
    LIGHTRING_MODE_WARM,
    LIGHTRING_MODE_COUNT,
} lightring_mode_t;

typedef enum {
    LIGHTRING_SPEED_OFF = 0,
    LIGHTRING_SPEED_LOW,
    LIGHTRING_SPEED_HIGH,
    LIGHTRING_SPEED_COUNT,
} lightring_speed_t;

typedef enum {
    LIGHTRING_STATUS_IDLE = 0,
    LIGHTRING_STATUS_MODE_CHANGING,
    LIGHTRING_STATUS_SPEED_COLLAPSING,
    LIGHTRING_STATUS_SPEED_WAITING_MAX,
    LIGHTRING_STATUS_SPEED_EXPANDING,
    LIGHTRING_STATUS_ACTIVATING,
    LIGHTRING_STATUS_DEACTIVATING,
    LIGHTRING_STATUS_COLD_CHANGING,
    LIGHTRING_STATUS_ERROR,
} lightring_status_t;

typedef struct {
    ws2812_config_t    ws2812;
    lightring_mode_t  initial_mode;
    lightring_speed_t initial_speed;
#ifdef ESP_PLATFORM
    uint32_t task_stack_bytes;
#else
    uint16_t task_stack_words;
#endif
    uint32_t task_priority;
} lightring_config_t;

int lightring_init(const lightring_config_t *config);
void lightring_deinit(void);

int lightring_change_mode(lightring_mode_t mode);
int lightring_change_speed(lightring_speed_t speed);
int lightring_cold_on(void);
int lightring_cold_off(void);

lightring_status_t lightring_get_status(void);
lightring_mode_t lightring_get_mode(void);
lightring_speed_t lightring_get_speed(void);
int lightring_is_cold(void);
