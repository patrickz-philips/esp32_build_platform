#include <math.h>

#include "lightring_effects.h"

#define LR_PI 3.14159265f

uint8_t lightring_framebuffer[LIGHTRING_LED_COUNT * 3U];

void lightring_set_pixel(uint16_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (index >= LIGHTRING_LED_COUNT) {
        return;
    }
    uint16_t offset = (uint16_t) (index * 3U);
    lightring_framebuffer[offset + 0U] = red;
    lightring_framebuffer[offset + 1U] = green;
    lightring_framebuffer[offset + 2U] = blue;
}

void lightring_clear(void)
{
    for (uint16_t i = 0U; i < (uint16_t) (LIGHTRING_LED_COUNT * 3U); ++i) {
        lightring_framebuffer[i] = 0U;
    }
}

/* Two mirrored arms grow outward from the 25/26 edge; pos 0 is that edge. */
static uint16_t left_index(uint8_t pos)
{
    return (uint16_t) ((LIGHTRING_LED_COUNT - 2U - pos) % LIGHTRING_LED_COUNT);
}

static uint16_t right_index(uint8_t pos)
{
    return (uint16_t) ((LIGHTRING_LED_COUNT - 1U + pos) % LIGHTRING_LED_COUNT);
}

static uint8_t scale8(uint8_t value, uint8_t scale)
{
    return (uint8_t) (((uint16_t) value * scale + 127U) / 255U);
}

static uint8_t mix8(uint8_t from, uint8_t to, uint8_t amount)
{
    return (uint8_t) ((((uint16_t) from * (uint16_t) (255U - amount)) +
                       ((uint16_t) to * amount) + 127U) / 255U);
}

static uint8_t breathe_level(float t, int rising)
{
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }
    float value = rising ? (0.5f * (1.0f - cosf(LR_PI * t)))
                         : (0.5f * (1.0f + cosf(LR_PI * t)));
    return (uint8_t) (value * 255.0f + 0.5f);
}

static void set_path_pixel(uint8_t pos, const lightring_palette_t *palette,
                           uint8_t palette_index, uint8_t brightness)
{
    uint8_t red = 0U;
    uint8_t green = 0U;
    uint8_t blue = 0U;

    lightring_sample_stops(palette, palette_index, &red, &green, &blue);
    red = scale8(red, brightness);
    green = scale8(green, brightness);
    blue = scale8(blue, brightness);
    lightring_set_pixel(left_index(pos), red, green, blue);
    lightring_set_pixel(right_index(pos), red, green, blue);
}

/* Spread a palette across the whole path (both arms), scaled by `level`. */
static void render_palette_path(const lightring_palette_t *palette, uint8_t level)
{
    uint8_t brightness = scale8(LIGHTRING_BRIGHTNESS, level);
    for (uint8_t pos = 0U; pos < LIGHTRING_PATH_LENGTH; ++pos) {
        set_path_pixel(pos, palette, lightring_compress_index(pos, LIGHTRING_PATH_LENGTH),
                       brightness);
    }
}

static void render_edge(const lightring_palette_t *palette, uint8_t level)
{
    set_path_pixel(0U, palette, 0U, scale8(LIGHTRING_BRIGHTNESS, level));
}

static void render_edge_blend(const lightring_palette_t *from,
                              const lightring_palette_t *to, uint8_t amount)
{
    uint8_t from_red;
    uint8_t from_green;
    uint8_t from_blue;
    uint8_t to_red;
    uint8_t to_green;
    uint8_t to_blue;

    lightring_sample_stops(from, 0U, &from_red, &from_green, &from_blue);
    lightring_sample_stops(to, 0U, &to_red, &to_green, &to_blue);
    lightring_set_pixel(left_index(0U),
                        scale8(mix8(from_red, to_red, amount), LIGHTRING_BRIGHTNESS),
                        scale8(mix8(from_green, to_green, amount), LIGHTRING_BRIGHTNESS),
                        scale8(mix8(from_blue, to_blue, amount), LIGHTRING_BRIGHTNESS));
    lightring_set_pixel(right_index(0U),
                        scale8(mix8(from_red, to_red, amount), LIGHTRING_BRIGHTNESS),
                        scale8(mix8(from_green, to_green, amount), LIGHTRING_BRIGHTNESS),
                        scale8(mix8(from_blue, to_blue, amount), LIGHTRING_BRIGHTNESS));
}

static void render_expand(const lightring_palette_t *palette, uint8_t cover_len)
{
    if (cover_len > LIGHTRING_PATH_LENGTH) {
        cover_len = LIGHTRING_PATH_LENGTH;
    }
    for (uint8_t pos = 0U; pos < cover_len; ++pos) {
        set_path_pixel(pos, palette, lightring_compress_index(pos, cover_len),
                       LIGHTRING_BRIGHTNESS);
    }
}

/* Darken from the far end toward LEDs 26/27. */
static void render_reverse_dark(uint8_t cover_len)
{
    if (cover_len > LIGHTRING_PATH_LENGTH) {
        cover_len = LIGHTRING_PATH_LENGTH;
    }
    uint8_t first = (uint8_t) (LIGHTRING_PATH_LENGTH - cover_len);
    for (uint8_t pos = first; pos < LIGHTRING_PATH_LENGTH; ++pos) {
        lightring_set_pixel(left_index(pos), 0U, 0U, 0U);
        lightring_set_pixel(right_index(pos), 0U, 0U, 0U);
    }
}

static uint8_t grow_cover(uint32_t elapsed, uint32_t total, uint8_t base, uint8_t span)
{
    if (total == 0U) {
        return (uint8_t) (base + span);
    }
    uint32_t value = (uint32_t) base + (((uint32_t) span * elapsed) / total);
    return (value > LIGHTRING_PATH_LENGTH) ? (uint8_t) LIGHTRING_PATH_LENGTH : (uint8_t) value;
}

uint8_t lightring_render_phase(const lightring_frame_t *frame)
{
    float t = (frame->total_ms > 0U) ? ((float) frame->elapsed_ms / (float) frame->total_ms)
                                     : 1.0f;
    uint8_t cover_len = 0U;

    switch (frame->phase) {
    case LR_PH_EXPAND:
        cover_len = grow_cover(frame->elapsed_ms, frame->total_ms, 1U,
                               (uint8_t) (LIGHTRING_PATH_LENGTH - 1U));
        lightring_clear();
        render_expand(frame->primary, cover_len);
        break;

    case LR_PH_COLLAPSE_TO_EDGE:
        cover_len = grow_cover(frame->elapsed_ms, frame->total_ms, 0U,
                               (uint8_t) (LIGHTRING_PATH_LENGTH - 1U));
        render_palette_path(frame->primary, 255U);
        render_reverse_dark(cover_len);
        break;

    case LR_PH_COLLAPSE_OFF:
        cover_len = grow_cover(frame->elapsed_ms, frame->total_ms, 0U,
                               LIGHTRING_PATH_LENGTH);
        render_palette_path(frame->primary, 255U);
        render_reverse_dark(cover_len);
        break;

    case LR_PH_HOLD_EDGE:
        lightring_clear();
        render_edge(frame->primary, 255U);
        break;

    case LR_PH_EDGE_FADE_OUT:
        lightring_clear();
        render_edge(frame->primary, breathe_level(t, 0));
        break;

    case LR_PH_EDGE_BLEND:
        lightring_clear();
        render_edge_blend(frame->primary, frame->secondary,
                          (uint8_t) (t * 255.0f + 0.5f));
        break;

    case LR_PH_HOLD:
        cover_len = LIGHTRING_PATH_LENGTH;
        render_palette_path(frame->primary, 255U);
        break;
    }

    return cover_len;
}
