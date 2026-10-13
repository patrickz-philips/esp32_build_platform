#pragma once

#include <stdint.h>

#include "lightring.h"
#include "lightring_config.h"

typedef struct {
    uint32_t             from_phase_ms;
    uint32_t             to_phase_ms;
    lightring_mode_t    from_mode;
    lightring_mode_t    to_mode;
    uint8_t              mode_mix;
    lightring_speed_t   from_speed;
    lightring_speed_t   to_speed;
    uint8_t              speed_mix;
    uint8_t              cold_mix;
} lightring_frame_t;

/* Nairobi-calibrated bytes in the device's GRB wire order. */
extern uint8_t lightring_framebuffer[LIGHTRING_LED_COUNT * 3U];

void lightring_render_frame(const lightring_frame_t *frame);
