#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lightring_effects.h"

static int pixel_is_lit(uint16_t index)
{
    uint16_t offset = (uint16_t) (index * 3U);
    return lightring_framebuffer[offset] != 0U ||
           lightring_framebuffer[offset + 1U] != 0U ||
           lightring_framebuffer[offset + 2U] != 0U;
}

static void render(lightring_phase_t phase, uint32_t elapsed_ms, uint32_t total_ms,
                   const lightring_palette_t *primary,
                   const lightring_palette_t *secondary)
{
    lightring_frame_t frame = {
        .phase = phase,
        .elapsed_ms = elapsed_ms,
        .total_ms = total_ms,
        .primary = primary,
        .secondary = secondary,
    };
    (void) lightring_render_phase(&frame);
}

int main(void)
{
    uint8_t expected[sizeof(lightring_framebuffer)];

    render(LR_PH_EXPAND, 350U, 350U, &LR_PAL_REGULAR, NULL);
    memcpy(expected, lightring_framebuffer, sizeof(expected));
    render(LR_PH_HOLD, 0U, 0U, &LR_PAL_REGULAR, NULL);
    assert(memcmp(expected, lightring_framebuffer, sizeof(expected)) == 0);

    render(LR_PH_COLLAPSE_TO_EDGE, 300U, 300U, &LR_PAL_REGULAR, NULL);
    for (uint16_t led = 0U; led < LIGHTRING_LED_COUNT; ++led) {
        assert(pixel_is_lit(led) == (led == 25U || led == 26U));
    }

    render(LR_PH_COLLAPSE_OFF, 350U, 350U, &LR_PAL_REGULAR, NULL);
    for (uint16_t led = 0U; led < LIGHTRING_LED_COUNT; ++led) {
        assert(!pixel_is_lit(led));
    }

    render(LR_PH_EDGE_FADE_OUT, 0U, 200U, &LR_PAL_REGULAR, NULL);
    assert(pixel_is_lit(25U) && pixel_is_lit(26U));
    render(LR_PH_EDGE_FADE_OUT, 200U, 200U, &LR_PAL_REGULAR, NULL);
    assert(!pixel_is_lit(25U) && !pixel_is_lit(26U));

    render(LR_PH_EDGE_BLEND, 200U, 200U, &LR_PAL_REGULAR, &LR_PAL_INTENSE);
    memcpy(expected, lightring_framebuffer, sizeof(expected));
    render(LR_PH_HOLD_EDGE, 0U, 0U, &LR_PAL_INTENSE, NULL);
    assert(memcmp(expected, lightring_framebuffer, sizeof(expected)) == 0);

    puts("lightring_v3 effects: ok");
    return 0;
}