#pragma once

#include <stddef.h>
#include <stdint.h>

/* A single WLED-style gradient stop: a palette index (0..255) and its color. */
typedef struct {
    uint8_t index;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} lightring_stop_t;

/* A hardcoded palette: a name plus an ordered, non-circular list of stops. */
typedef struct {
    const char             *name;
    uint8_t                 stop_count;
    const lightring_stop_t *stops;
} lightring_palette_t;

/* Palettes used by the ported effects (values baked from the source device;
   the component never reads any JSON or file at runtime). */
extern const lightring_palette_t LR_PAL_BLACK;     /* #2 Black     */
extern const lightring_palette_t LR_PAL_INTENSE;   /* #4 Intense   */
extern const lightring_palette_t LR_PAL_SENSITIVE; /* #5 Sensitive */
extern const lightring_palette_t LR_PAL_REGULAR;   /* #6 Regular   */

/* Map pixel `pos` within a run of `length` pixels to a palette index (0..255),
   scaling the whole palette evenly across the run. */
uint8_t lightring_compress_index(uint8_t pos, uint8_t length);

/* Sample a palette at index `target` (0..255) with linear interpolation. */
void lightring_sample_stops(const lightring_palette_t *palette, uint8_t target,
                            uint8_t *red, uint8_t *green, uint8_t *blue);
