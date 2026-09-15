#pragma once

#include <stdint.h>

#include "lightring_config.h"

typedef struct {
   int      gpio_num;
    uint16_t led_count;
} ws2812_config_t;

/* 0 on success, non-zero on error. */
int ws2812_init(const ws2812_config_t *config);

/* Send `led_count` LEDs from `rgb` (three bytes R, G, B per LED). Blocks until
   the frame has been shifted out. */
int ws2812_send(const uint8_t *rgb, uint16_t led_count);
void ws2812_deinit(void);
