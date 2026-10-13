#pragma once

#include <stdint.h>

#define LIGHTRING_LED_COUNT        24U
#define LIGHTRING_EFFECT_PERIOD_MS 2500U
#define LIGHTRING_TRANSITION_MS    300U
#define LIGHTRING_FRAME_MS         10U

#ifndef ESP_PLATFORM
typedef enum {
    LIGHTRING_WS2812_TIM_DMA = 0,
    LIGHTRING_WS2812_SPI_DMA,
} lightring_ws2812_backend_t;
#endif
