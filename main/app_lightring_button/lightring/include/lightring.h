#pragma once

#include <stdint.h>

#include "ws2812.h"

/* Reported light-effect status (the component's output). */
typedef enum {
    LIGHTRING_MODE_INTENSE = 0,
    LIGHTRING_MODE_SENSITIVE,
    LIGHTRING_MODE_REGULAR,
    LIGHTRING_MODE_COUNT,
} lightring_mode_t;

typedef enum {
    LIGHTRING_STATUS_OFF = 0,
    LIGHTRING_STATUS_ACTIVATING,
    LIGHTRING_STATUS_ON,
    LIGHTRING_STATUS_DEACTIVATING,
    LIGHTRING_STATUS_MODE_COLLAPSING,
    LIGHTRING_STATUS_MODE_WAITING,
    LIGHTRING_STATUS_MODE_PREVIEW,
    LIGHTRING_STATUS_MODE_APPLYING,
} lightring_status_t;

typedef struct {
    ws2812_config_t ws2812;
#ifdef ESP_PLATFORM
    uint32_t        task_stack_bytes; /* 0 -> default */
#else
    uint16_t        task_stack_words; /* 0 -> default */
#endif
    uint32_t        task_priority;    /* 0 -> default */
} lightring_config_t;

/* Initialize the WS2812 backend and start the internal render task. Returns 0
   on success. Call once, after the RTOS scheduler is available. */
int lightring_init(const lightring_config_t *config);

/* Domain events. Return 0 when accepted, or -1 when the event is invalid for
    the current state, the mode is invalid, or the component is not initialized. */
int lightring_activate(lightring_mode_t mode);
int lightring_deactivate(void);
int lightring_begin_mode_change(lightring_mode_t next_mode);
int lightring_cancel_mode_change(void);
int lightring_commit_mode_change(void);

lightring_status_t lightring_get_status(void);
lightring_mode_t lightring_get_mode(void);
