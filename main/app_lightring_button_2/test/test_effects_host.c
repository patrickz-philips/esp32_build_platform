#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lightring_button_2_effects.h"

static uint16_t left_index(uint8_t position)
{
    return (uint16_t)((LIGHTRING_LED_COUNT - 2U - position) % LIGHTRING_LED_COUNT);
}

static uint16_t right_index(uint8_t position)
{
    return (uint16_t)((LIGHTRING_LED_COUNT - 1U + position) % LIGHTRING_LED_COUNT);
}

static lightring_button_2_mode_t pixel_mode(uint16_t index)
{
    const uint16_t offset = (uint16_t)(index * 3U);
    if (lightring_button_2_framebuffer[offset] != 0U) {
        return LIGHTRING_BUTTON_2_MODE_RED;
    }
    if (lightring_button_2_framebuffer[offset + 1U] != 0U) {
        return LIGHTRING_BUTTON_2_MODE_GREEN;
    }
    return LIGHTRING_BUTTON_2_MODE_BLUE;
}

static void assert_path_mode(uint8_t position, lightring_button_2_mode_t mode)
{
    assert(pixel_mode(left_index(position)) == mode);
    assert(pixel_mode(right_index(position)) == mode);
}

int main(void)
{
    uint8_t frozen[sizeof(lightring_button_2_framebuffer)];

    assert(lightring_button_2_decide(749U, 0) ==
           LIGHTRING_BUTTON_2_DECISION_CONTINUE);
    assert(lightring_button_2_decide(749U, 1) ==
           LIGHTRING_BUTTON_2_DECISION_RESTORE);
    assert(lightring_button_2_decide(750U, 0) ==
           LIGHTRING_BUTTON_2_DECISION_COMMIT);
    assert(lightring_button_2_decide(750U, 1) ==
           LIGHTRING_BUTTON_2_DECISION_COMMIT);

    lightring_button_2_render_solid(LIGHTRING_BUTTON_2_MODE_RED);
    for (uint16_t led = 0U; led < LIGHTRING_LED_COUNT; ++led) {
        assert(pixel_mode(led) == LIGHTRING_BUTTON_2_MODE_RED);
    }

    lightring_button_2_render_preview(0U);
    assert_path_mode(0U, LIGHTRING_BUTTON_2_MODE_RED);
    assert_path_mode(4U, LIGHTRING_BUTTON_2_MODE_RED);
    assert_path_mode(5U, LIGHTRING_BUTTON_2_MODE_GREEN);
    assert_path_mode(9U, LIGHTRING_BUTTON_2_MODE_GREEN);
    assert_path_mode(10U, LIGHTRING_BUTTON_2_MODE_BLUE);
    assert_path_mode(13U, LIGHTRING_BUTTON_2_MODE_BLUE);
    memcpy(frozen, lightring_button_2_framebuffer, sizeof(frozen));

    lightring_button_2_render_preview(400U);
    assert(memcmp(frozen, lightring_button_2_framebuffer, sizeof(frozen)) == 0);

    lightring_button_2_render_preview(200U);
    assert_path_mode(7U, LIGHTRING_BUTTON_2_MODE_RED);
    assert_path_mode(0U, LIGHTRING_BUTTON_2_MODE_GREEN);
    assert_path_mode(3U, LIGHTRING_BUTTON_2_MODE_BLUE);

    lightring_button_2_render_solid(LIGHTRING_BUTTON_2_MODE_GREEN);
    for (uint16_t led = 0U; led < LIGHTRING_LED_COUNT; ++led) {
        assert(pixel_mode(led) == LIGHTRING_BUTTON_2_MODE_GREEN);
    }

    puts("lightring_button_2 effects: ok");
    return 0;
}
