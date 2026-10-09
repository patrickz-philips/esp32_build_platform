#include "lightring_button_2_effects.h"

#define PREVIEW_PERIOD_MS 400U
#define HOLD_PERIOD_MS    750U

uint8_t lightring_button_2_framebuffer[LIGHTRING_LED_COUNT * 3U];

lightring_button_2_decision_t lightring_button_2_decide(uint32_t held_ms,
                                                        int released)
{
    if (held_ms >= HOLD_PERIOD_MS) {
        return LIGHTRING_BUTTON_2_DECISION_COMMIT;
    }
    return released ? LIGHTRING_BUTTON_2_DECISION_RESTORE
                    : LIGHTRING_BUTTON_2_DECISION_CONTINUE;
}

static uint16_t left_index(uint8_t position)
{
    return (uint16_t)((LIGHTRING_LED_COUNT - 2U - position) % LIGHTRING_LED_COUNT);
}

static uint16_t right_index(uint8_t position)
{
    return (uint16_t)((LIGHTRING_LED_COUNT - 1U + position) % LIGHTRING_LED_COUNT);
}

static void mode_color(lightring_button_2_mode_t mode,
                       uint8_t *red, uint8_t *green, uint8_t *blue)
{
    *red = mode == LIGHTRING_BUTTON_2_MODE_RED ? LIGHTRING_BRIGHTNESS : 0U;
    *green = mode == LIGHTRING_BUTTON_2_MODE_GREEN ? LIGHTRING_BRIGHTNESS : 0U;
    *blue = mode == LIGHTRING_BUTTON_2_MODE_BLUE ? LIGHTRING_BRIGHTNESS : 0U;
}

static void set_pixel(uint16_t index, lightring_button_2_mode_t mode)
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    mode_color(mode, &red, &green, &blue);

    const uint16_t offset = (uint16_t)(index * 3U);
    lightring_button_2_framebuffer[offset] = red;
    lightring_button_2_framebuffer[offset + 1U] = green;
    lightring_button_2_framebuffer[offset + 2U] = blue;
}

static void set_path_pixel(uint8_t position, lightring_button_2_mode_t mode)
{
    set_pixel(left_index(position), mode);
    set_pixel(right_index(position), mode);
}

void lightring_button_2_render_solid(lightring_button_2_mode_t mode)
{
    for (uint8_t position = 0U; position < LIGHTRING_PATH_LENGTH; ++position) {
        set_path_pixel(position, mode);
    }
}

void lightring_button_2_render_preview(uint32_t elapsed_ms)
{
    const uint8_t offset =
        (uint8_t)(((elapsed_ms % PREVIEW_PERIOD_MS) * LIGHTRING_PATH_LENGTH) /
                  PREVIEW_PERIOD_MS);

    for (uint8_t position = 0U; position < LIGHTRING_PATH_LENGTH; ++position) {
        const uint8_t source =
            (uint8_t)((position + LIGHTRING_PATH_LENGTH - offset) % LIGHTRING_PATH_LENGTH);
        const lightring_button_2_mode_t mode =
            (lightring_button_2_mode_t)((source * LIGHTRING_BUTTON_2_MODE_COUNT) /
                                        LIGHTRING_PATH_LENGTH);
        set_path_pixel(position, mode);
    }
}
