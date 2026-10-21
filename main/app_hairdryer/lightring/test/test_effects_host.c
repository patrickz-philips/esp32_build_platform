#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lightring_effects.h"

static const uint8_t *pixel(uint8_t physical)
{
    return &lightring_framebuffer[physical * 3U];
}

static void assert_pixel(uint8_t physical, uint8_t green, uint8_t red,
                         uint8_t blue)
{
    const uint8_t *value = pixel(physical);
    if (value[0] != green || value[1] != red || value[2] != blue) {
        fprintf(stderr, "pixel %u: expected GRB %u/%u/%u, got %u/%u/%u\n",
                physical, green, red, blue, value[0], value[1], value[2]);
        assert(0);
    }
}

static lightring_frame_t frame(lightring_mode_t mode,
                                lightring_speed_t speed,
                                uint32_t phase_ms)
{
    lightring_frame_t value = {
        .from_phase_ms = phase_ms,
        .to_phase_ms = phase_ms,
        .from_mode = mode,
        .to_mode = mode,
        .mode_mix = 0U,
        .from_speed = speed,
        .to_speed = speed,
        .speed_mix = 0U,
        .cold_mix = 0U,
    };
    return value;
}

static int dump_frames(void)
{
    for (int mode = 0; mode < LIGHTRING_MODE_COUNT; ++mode) {
        for (int speed = 0; speed < LIGHTRING_SPEED_COUNT; ++speed) {
            for (int cold = 0; cold <= 1; ++cold) {
                for (uint32_t phase_ms = 0U;
                     phase_ms <= LIGHTRING_PHASE_PERIOD_MS + LIGHTRING_FRAME_MS;
                     phase_ms += LIGHTRING_FRAME_MS) {
                    lightring_frame_t value = frame(
                        (lightring_mode_t)mode, (lightring_speed_t)speed, phase_ms);
                    value.cold_mix = cold ? 255U : 0U;
                    lightring_render_frame(&value);
                    if (fwrite(lightring_framebuffer, 1U,
                               sizeof(lightring_framebuffer), stdout) !=
                        sizeof(lightring_framebuffer)) {
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--dump") == 0) {
        return dump_frames();
    }
    assert(argc == 1);
    lightring_frame_t value = frame(
        LIGHTRING_MODE_THERMO, LIGHTRING_SPEED_HIGH, 0U);
    lightring_render_frame(&value);
    assert_pixel(0U, 0U, 9U, 13U);
    assert_pixel(7U, 0U, 14U, 13U);
    assert_pixel(8U, 1U, 26U, 37U);

    uint8_t first[sizeof(lightring_framebuffer)];
    memcpy(first, lightring_framebuffer, sizeof(first));
    value.from_phase_ms = LIGHTRING_THERMO_PERIOD_MS;
    value.to_phase_ms = LIGHTRING_THERMO_PERIOD_MS;
    lightring_render_frame(&value);
    assert(memcmp(first, lightring_framebuffer, sizeof(first)) == 0);

    value.from_phase_ms = 2500U;
    value.to_phase_ms = 2500U;
    lightring_render_frame(&value);
    assert(memcmp(first, lightring_framebuffer, sizeof(first)) != 0);

    value.from_phase_ms = 1000U;
    value.to_phase_ms = 1000U;
    lightring_render_frame(&value);
    assert_pixel(0U, 3U, 4U, 13U);
    assert_pixel(8U, 9U, 12U, 37U);

    value = frame(LIGHTRING_MODE_HOT, LIGHTRING_SPEED_LOW, 1000U);
    lightring_render_frame(&value);
    assert_pixel(0U, 15U, 129U, 5U);
    assert_pixel(8U, 0U, 0U, 0U);

    value.to_mode = LIGHTRING_MODE_WARM;
    value.mode_mix = 255U;
    lightring_render_frame(&value);
    assert_pixel(0U, 65U, 129U, 1U);

    value.cold_mix = 255U;
    lightring_render_frame(&value);
    assert_pixel(0U, 46U, 34U, 81U);
    assert_pixel(5U, 86U, 78U, 82U);
    assert_pixel(4U, 115U, 138U, 82U);

    value = frame(LIGHTRING_MODE_THERMO, LIGHTRING_SPEED_HIGH, 1000U);
    value.to_speed = LIGHTRING_SPEED_LOW;
    value.speed_mix = 255U;
    lightring_render_frame(&value);
    assert_pixel(0U, 20U, 27U, 81U);
    assert_pixel(8U, 0U, 0U, 0U);

    value = frame(LIGHTRING_MODE_THERMO, LIGHTRING_SPEED_LOW, 1000U);
    value.to_speed = LIGHTRING_SPEED_HIGH;
    value.speed_mix = 255U;
    lightring_render_frame(&value);
    assert_pixel(0U, 3U, 4U, 13U);
    assert_pixel(8U, 9U, 12U, 37U);

    value = frame(LIGHTRING_MODE_HOT, LIGHTRING_SPEED_LOW, 1000U);
    value.to_speed = LIGHTRING_SPEED_OFF;
    value.speed_mix = 255U;
    lightring_render_frame(&value);
    for (uint8_t led = 0U; led < LIGHTRING_LED_COUNT; ++led) {
        assert_pixel(led, 0U, 0U, 0U);
    }

    value = frame(LIGHTRING_MODE_HOT, LIGHTRING_SPEED_OFF, 1000U);
    value.to_speed = LIGHTRING_SPEED_LOW;
    value.speed_mix = 255U;
    lightring_render_frame(&value);
    assert_pixel(0U, 15U, 129U, 5U);

    value = frame(LIGHTRING_MODE_HOT, LIGHTRING_SPEED_OFF, 1000U);
    value.to_speed = LIGHTRING_SPEED_HIGH;
    value.speed_mix = 255U;
    lightring_render_frame(&value);
    assert_pixel(0U, 2U, 19U, 0U);
    assert_pixel(8U, 7U, 56U, 2U);

    puts("lightring effects: ok");
    return 0;
}
