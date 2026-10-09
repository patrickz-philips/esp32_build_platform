#include "lightring_palette.h"

/* Palette stops transcribed from the source device catalog. Stop layout is
   {index, R, G, B}; #2 has a single stop, so it is a solid color. */

static const lightring_stop_t s_black_stops[] = {
    { 255, 0, 0, 0 },
};

static const lightring_stop_t s_intense_stops[] = {
    { 0, 255, 157, 0 },
    { 255, 255, 229, 92 },
};

static const lightring_stop_t s_sensitive_stops[] = {
    { 0, 0, 216, 255 },
    { 255, 124, 253, 255 },
};

static const lightring_stop_t s_regular_stops[] = {
    { 0, 220, 226, 234 },
    { 255, 255, 255, 255 },
};

const lightring_palette_t LR_PAL_BLACK = {
    .name = "Black",
    .stop_count = (uint8_t) (sizeof(s_black_stops) / sizeof(s_black_stops[0])),
    .stops = s_black_stops,
};

const lightring_palette_t LR_PAL_INTENSE = {
    .name = "Intense",
    .stop_count = (uint8_t) (sizeof(s_intense_stops) / sizeof(s_intense_stops[0])),
    .stops = s_intense_stops,
};

const lightring_palette_t LR_PAL_SENSITIVE = {
    .name = "Sensitive",
    .stop_count = (uint8_t) (sizeof(s_sensitive_stops) / sizeof(s_sensitive_stops[0])),
    .stops = s_sensitive_stops,
};

const lightring_palette_t LR_PAL_REGULAR = {
    .name = "Regular",
    .stop_count = (uint8_t) (sizeof(s_regular_stops) / sizeof(s_regular_stops[0])),
    .stops = s_regular_stops,
};

uint8_t lightring_compress_index(uint8_t pos, uint8_t length)
{
    if (length <= 1U || pos == 0U) {
        return 0U;
    }
    if (pos >= (uint8_t) (length - 1U)) {
        return 255U;
    }

    uint32_t index = (((uint32_t) pos * 255U) + ((length - 1U) / 2U)) / (uint32_t) (length - 1U);
    return (index > 255U) ? 255U : (uint8_t) index;
}

static void sample_gradient(const lightring_stop_t *left, const lightring_stop_t *right,
                            uint16_t span, uint16_t distance,
                            uint8_t *red, uint8_t *green, uint8_t *blue)
{
    if (span == 0U) {
        *red = right->red;
        *green = right->green;
        *blue = right->blue;
        return;
    }

    uint16_t progress = (uint16_t) ((distance * 255U) / span);
    uint16_t remain = (uint16_t) (255U - progress);

    *red = (uint8_t) ((((uint16_t) left->red * remain) + ((uint16_t) right->red * progress)) / 255U);
    *green = (uint8_t) ((((uint16_t) left->green * remain) + ((uint16_t) right->green * progress)) / 255U);
    *blue = (uint8_t) ((((uint16_t) left->blue * remain) + ((uint16_t) right->blue * progress)) / 255U);
}

void lightring_sample_stops(const lightring_palette_t *palette, uint8_t target,
                            uint8_t *red, uint8_t *green, uint8_t *blue)
{
    const lightring_stop_t *stops = palette->stops;
    size_t count = palette->stop_count;

    if (count == 0U) {
        *red = 0U;
        *green = 0U;
        *blue = 0U;
        return;
    }

    if (count == 1U) {
        *red = stops[0].red;
        *green = stops[0].green;
        *blue = stops[0].blue;
        return;
    }

    const lightring_stop_t *left = &stops[0];
    const lightring_stop_t *right = &stops[count - 1U];

    if (target <= left->index) {
        *red = left->red;
        *green = left->green;
        *blue = left->blue;
        return;
    }
    if (target >= right->index) {
        *red = right->red;
        *green = right->green;
        *blue = right->blue;
        return;
    }

    for (size_t stop = 1U; stop < count; ++stop) {
        right = &stops[stop];
        if (target > right->index) {
            left = right;
            continue;
        }
        sample_gradient(left, right, (uint16_t) (right->index - left->index),
                        (uint16_t) (target - left->index), red, green, blue);
        return;
    }
}
