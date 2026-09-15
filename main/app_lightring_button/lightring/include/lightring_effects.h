#pragma once

#include <stdint.h>

#include "lightring_config.h"
#include "lightring_palette.h"

/* Stateless render phases used by the event-driven orchestrator. */
typedef enum {
    LR_PH_EXPAND,
    LR_PH_COLLAPSE_TO_EDGE,
    LR_PH_COLLAPSE_OFF,
    LR_PH_HOLD_EDGE,
    LR_PH_EDGE_FADE_OUT,
    LR_PH_EDGE_BLEND,
    LR_PH_HOLD,
} lightring_phase_t;

/* One render request. total_ms == 0 renders the final frame of the phase. */
typedef struct {
    lightring_phase_t          phase;
    uint32_t                   elapsed_ms;
    uint32_t                   total_ms;
    const lightring_palette_t *primary;
    const lightring_palette_t *secondary;
} lightring_frame_t;

/* RGB framebuffer, three bytes (R, G, B) per LED, in physical LED order. */
extern uint8_t lightring_framebuffer[LIGHTRING_LED_COUNT * 3U];

void lightring_set_pixel(uint16_t index, uint8_t red, uint8_t green, uint8_t blue);
void lightring_clear(void);

/* Render one frame and return the number of covered path positions. */
uint8_t lightring_render_phase(const lightring_frame_t *frame);
