#include <stddef.h>

#include "lightring_effects.h"
#include "lightring_calibration.inc"

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} lr_color_t;

static const uint8_t s_logical_to_physical[LIGHTRING_LED_COUNT] = {
    0U, 7U, 6U, 5U, 4U, 3U, 2U, 1U,
    10U, 9U, 8U, 23U, 22U, 21U, 20U, 19U,
    18U, 17U, 16U, 15U, 14U, 13U, 12U, 11U,
};

static const uint8_t s_color_group[LIGHTRING_LED_COUNT] = {
    0U, 0U, 0U, 1U, 2U, 2U, 1U, 1U,
    1U, 1U, 0U, 0U, 0U, 0U, 0U, 1U,
    1U, 1U, 2U, 2U, 2U, 1U, 1U, 1U,
};

static const lr_color_t s_mode_colors[LIGHTRING_MODE_COUNT][3] = {
    [LIGHTRING_MODE_HOT] = {
        { 255U, 64U, 64U },
        { 255U, 95U, 95U },
        { 255U, 123U, 123U },
    },
    [LIGHTRING_MODE_WARM] = {
        { 231U, 174U, 0U },
        { 255U, 211U, 79U },
        { 221U, 189U, 91U },
    },
};

/* Exact 500 ms JSON keyframes in logical order; blue is always 255. */
static const uint8_t s_thermo_rg[LIGHTRING_LED_COUNT][8][2] = {
    { { 169U, 50U }, { 101U, 66U }, { 76U, 113U }, { 51U, 160U }, { 84U, 97U }, { 139U, 57U }, { 177U, 48U }, { 215U, 39U } },
    { { 215U, 39U }, { 169U, 50U }, { 101U, 66U }, { 76U, 113U }, { 51U, 160U }, { 84U, 97U }, { 139U, 57U }, { 177U, 48U } },
    { { 177U, 48U }, { 215U, 39U }, { 169U, 50U }, { 101U, 66U }, { 76U, 113U }, { 51U, 160U }, { 84U, 97U }, { 139U, 57U } },
    { { 139U, 57U }, { 177U, 48U }, { 215U, 39U }, { 169U, 50U }, { 101U, 66U }, { 76U, 113U }, { 51U, 160U }, { 84U, 97U } },
    { { 84U, 97U }, { 139U, 57U }, { 177U, 48U }, { 215U, 39U }, { 169U, 50U }, { 101U, 66U }, { 76U, 113U }, { 51U, 160U } },
    { { 51U, 160U }, { 84U, 97U }, { 139U, 57U }, { 177U, 48U }, { 215U, 39U }, { 169U, 50U }, { 101U, 66U }, { 76U, 113U } },
    { { 76U, 113U }, { 51U, 160U }, { 84U, 97U }, { 139U, 57U }, { 177U, 48U }, { 215U, 39U }, { 169U, 50U }, { 101U, 66U } },
    { { 101U, 66U }, { 76U, 113U }, { 51U, 160U }, { 84U, 97U }, { 139U, 57U }, { 177U, 48U }, { 215U, 39U }, { 169U, 50U } },
    { { 101U, 66U }, { 76U, 113U }, { 51U, 160U }, { 84U, 97U }, { 139U, 57U }, { 177U, 48U }, { 215U, 39U }, { 169U, 50U } },
    { { 147U, 55U }, { 89U, 90U }, { 64U, 137U }, { 68U, 129U }, { 101U, 66U }, { 158U, 53U }, { 196U, 44U }, { 192U, 44U } },
    { { 169U, 50U }, { 101U, 66U }, { 76U, 113U }, { 51U, 160U }, { 84U, 97U }, { 139U, 57U }, { 177U, 48U }, { 215U, 39U } },
    { { 192U, 44U }, { 149U, 55U }, { 89U, 90U }, { 64U, 137U }, { 68U, 129U }, { 101U, 66U }, { 158U, 53U }, { 196U, 44U } },
    { { 215U, 39U }, { 169U, 50U }, { 101U, 66U }, { 76U, 113U }, { 51U, 160U }, { 84U, 97U }, { 139U, 57U }, { 177U, 48U } },
    { { 196U, 44U }, { 192U, 44U }, { 149U, 55U }, { 89U, 90U }, { 64U, 137U }, { 68U, 129U }, { 101U, 66U }, { 158U, 53U } },
    { { 177U, 48U }, { 215U, 39U }, { 169U, 50U }, { 101U, 66U }, { 76U, 113U }, { 51U, 160U }, { 84U, 97U }, { 139U, 57U } },
    { { 158U, 53U }, { 196U, 44U }, { 192U, 44U }, { 149U, 55U }, { 89U, 90U }, { 64U, 137U }, { 68U, 129U }, { 101U, 66U } },
    { { 139U, 57U }, { 177U, 48U }, { 215U, 39U }, { 169U, 50U }, { 101U, 66U }, { 76U, 113U }, { 51U, 160U }, { 84U, 97U } },
    { { 101U, 66U }, { 158U, 53U }, { 196U, 44U }, { 192U, 44U }, { 147U, 55U }, { 89U, 90U }, { 64U, 137U }, { 68U, 129U } },
    { { 84U, 97U }, { 139U, 57U }, { 177U, 48U }, { 215U, 39U }, { 169U, 50U }, { 101U, 66U }, { 76U, 113U }, { 51U, 160U } },
    { { 68U, 129U }, { 101U, 66U }, { 158U, 53U }, { 196U, 44U }, { 192U, 44U }, { 147U, 55U }, { 89U, 90U }, { 64U, 137U } },
    { { 51U, 160U }, { 84U, 97U }, { 139U, 57U }, { 177U, 48U }, { 215U, 39U }, { 169U, 50U }, { 101U, 66U }, { 76U, 113U } },
    { { 64U, 137U }, { 68U, 129U }, { 101U, 66U }, { 158U, 53U }, { 196U, 44U }, { 192U, 44U }, { 147U, 55U }, { 89U, 90U } },
    { { 76U, 113U }, { 51U, 160U }, { 84U, 97U }, { 139U, 57U }, { 177U, 48U }, { 215U, 39U }, { 169U, 50U }, { 101U, 66U } },
    { { 89U, 90U }, { 64U, 137U }, { 68U, 129U }, { 101U, 66U }, { 158U, 53U }, { 196U, 44U }, { 192U, 44U }, { 147U, 55U } },
};

static const lr_color_t s_cold_colors[3] = {
    { 51U, 160U, 255U },
    { 134U, 208U, 255U },
    { 199U, 233U, 255U },
};

uint8_t lightring_framebuffer[LIGHTRING_LED_COUNT * 3U];

static uint8_t mix8(uint8_t from, uint8_t to, uint8_t amount)
{
    return (uint8_t)((((uint16_t)from * (uint16_t)(255U - amount)) +
                      ((uint16_t)to * amount) + 127U) / 255U);
}

static uint8_t scale8(uint8_t value, uint8_t scale)
{
    return (uint8_t)(((uint16_t)value * scale) / 255U);
}

static uint8_t linear_mix(uint8_t from, uint8_t to, uint32_t elapsed, uint32_t duration)
{
    uint32_t weighted = ((uint32_t)from * (duration - elapsed)) +
                        ((uint32_t)to * elapsed);
    return (uint8_t)((weighted + duration / 2U) / duration);
}

static uint8_t track_brightness(lightring_mode_t mode, lightring_speed_t speed,
                                int outer, uint32_t phase_ms)
{
    if (speed == LIGHTRING_SPEED_OFF) {
        return 0U;
    }
    if (speed == LIGHTRING_SPEED_LOW && outer) {
        return 0U;
    }
    if (mode == LIGHTRING_MODE_THERMO) {
        return speed == LIGHTRING_SPEED_LOW ? 255U : (outer ? 200U : 80U);
    }

    uint8_t start;
    uint8_t peak;
    if (speed == LIGHTRING_SPEED_LOW) {
        start = 160U;
        peak = 255U;
    } else if (outer) {
        start = 40U;
        peak = 200U;
    } else {
        start = 230U;
        peak = 80U;
    }

    phase_ms %= LIGHTRING_EFFECT_PERIOD_MS;
    if (phase_ms < 500U) {
        return start;
    }
    if (phase_ms < 1000U) {
        return linear_mix(start, peak, phase_ms - 500U, 500U);
    }
    if (phase_ms < 1900U) {
        return peak;
    }
    return linear_mix(peak, start, phase_ms - 1900U, 600U);
}

static lr_color_t mix_color(lr_color_t from, lr_color_t to, uint8_t amount)
{
    lr_color_t color = {
        .red = mix8(from.red, to.red, amount),
        .green = mix8(from.green, to.green, amount),
        .blue = mix8(from.blue, to.blue, amount),
    };
    return color;
}

static lr_color_t mode_color(lightring_mode_t mode, uint8_t logical,
                              uint32_t phase_ms)
{
    if (mode != LIGHTRING_MODE_THERMO) {
        return s_mode_colors[mode][s_color_group[logical]];
    }
    phase_ms %= LIGHTRING_THERMO_PERIOD_MS;
    uint8_t key = (uint8_t)(phase_ms / 500U);
    uint8_t next = (uint8_t)((key + 1U) % 8U);
    const uint8_t *from = s_thermo_rg[logical][key];
    const uint8_t *to = s_thermo_rg[logical][next];
    lr_color_t color = {
        .red = linear_mix(from[0], to[0], phase_ms % 500U, 500U),
        .green = linear_mix(from[1], to[1], phase_ms % 500U, 500U),
        .blue = 255U,
    };
    return color;
}

static lr_color_t effect_color(const lightring_frame_t *frame, uint8_t logical,
                                uint32_t phase_ms)
{
    lr_color_t color = mix_color(
        mode_color(frame->from_mode, logical, phase_ms),
        mode_color(frame->to_mode, logical, phase_ms), frame->mode_mix);
    return mix_color(color, s_cold_colors[s_color_group[logical]], frame->cold_mix);
}

static uint8_t effect_brightness(const lightring_frame_t *frame,
                                 lightring_speed_t speed, int outer,
                                 uint32_t phase_ms)
{
    uint8_t level = mix8(
        track_brightness(frame->from_mode, speed, outer, phase_ms),
        track_brightness(frame->to_mode, speed, outer, phase_ms), frame->mode_mix);
    return mix8(level, track_brightness(LIGHTRING_MODE_HOT, speed, outer, phase_ms),
                frame->cold_mix);
}

static uint8_t clamp_channel(int32_t value)
{
    if (value < 0) {
        return 0U;
    }
    if (value > 255) {
        return 255U;
    }
    return (uint8_t)value;
}

static lr_color_t calibrated_color(lr_color_t color, uint8_t brightness)
{
    int32_t red = s_srgb_lut[color.red];
    int32_t green = s_srgb_lut[color.green];
    int32_t blue = s_srgb_lut[color.blue];
    lr_color_t corrected = {
        .red = clamp_channel((2342 * red + 1302 * green + 157 * blue) / 4096),
        .green = clamp_channel((141 * red + 2339 * green - 52 * blue) / 4096),
        .blue = clamp_channel((18 * red + 24 * green + 1351 * blue) / 4096),
    };
    uint8_t level = s_brightness_lut[brightness];
    corrected.red = s_red_lut[scale8(corrected.red, level)];
    corrected.green = s_green_lut[scale8(corrected.green, level)];
    corrected.blue = s_blue_lut[scale8(corrected.blue, level)];
    return corrected;
}

void lightring_render_frame(const lightring_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    uint8_t from_inner = effect_brightness(
        frame, frame->from_speed, 0, frame->from_phase_ms);
    uint8_t from_outer = effect_brightness(
        frame, frame->from_speed, 1, frame->from_phase_ms);
    uint8_t to_inner = effect_brightness(
        frame, frame->to_speed, 0, frame->to_phase_ms);
    uint8_t to_outer = effect_brightness(
        frame, frame->to_speed, 1, frame->to_phase_ms);

    for (uint8_t logical = 0U; logical < LIGHTRING_LED_COUNT; ++logical) {
        int outer = logical >= 8U;
        uint8_t from_level = outer ? from_outer : from_inner;
        uint8_t to_level = outer ? to_outer : to_inner;
        uint8_t level = mix8(from_level, to_level, frame->speed_mix);
        lr_color_t color = mix_color(
            effect_color(frame, logical, frame->from_phase_ms),
            effect_color(frame, logical, frame->to_phase_ms), frame->speed_mix);
        color = calibrated_color(color, level);
        uint16_t offset = (uint16_t)(s_logical_to_physical[logical] * 3U);
        lightring_framebuffer[offset] = color.green;
        lightring_framebuffer[offset + 1U] = color.red;
        lightring_framebuffer[offset + 2U] = color.blue;
    }
}
