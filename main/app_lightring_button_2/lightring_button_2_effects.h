#pragma once

#include <stdint.h>

#include "lightring_config.h"

typedef enum {
    LIGHTRING_BUTTON_2_MODE_RED = 0,
    LIGHTRING_BUTTON_2_MODE_GREEN,
    LIGHTRING_BUTTON_2_MODE_BLUE,
    LIGHTRING_BUTTON_2_MODE_COUNT,
} lightring_button_2_mode_t;

typedef enum {
    LIGHTRING_BUTTON_2_DECISION_CONTINUE = 0,
    LIGHTRING_BUTTON_2_DECISION_RESTORE,
    LIGHTRING_BUTTON_2_DECISION_COMMIT,
} lightring_button_2_decision_t;

extern uint8_t lightring_button_2_framebuffer[LIGHTRING_LED_COUNT * 3U];

lightring_button_2_decision_t lightring_button_2_decide(uint32_t held_ms,
                                                        int released);
void lightring_button_2_render_solid(lightring_button_2_mode_t mode);
void lightring_button_2_render_preview(uint32_t elapsed_ms);
