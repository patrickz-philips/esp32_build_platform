#pragma once

/* Compile-time configuration for the 27-LED light-ring effect component. */

#include <stdint.h>

#define LIGHTRING_LED_COUNT    27U
#define LIGHTRING_PATH_LENGTH  ((LIGHTRING_LED_COUNT + 1U) / 2U) /* 14 */
#define LIGHTRING_BRIGHTNESS   140U
#define LIGHTRING_FRAME_MS     15U
