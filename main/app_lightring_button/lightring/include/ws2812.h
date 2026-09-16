#pragma once

#include <stdint.h>

#include "lightring_config.h"

typedef struct {
#ifdef ESP_PLATFORM
   int gpio_num;
#else
   lightring_ws2812_backend_t backend;
   void                     *tim;
   uint32_t                  tim_channel;
   void                     *spi;
#endif
    uint16_t led_count;
} ws2812_config_t;

/* 0 on success, non-zero on error. */
int ws2812_init(const ws2812_config_t *config);

/* Send `led_count` LEDs from `rgb` (three bytes R, G, B per LED). Blocks until
   the frame has been shifted out. */
int ws2812_send(const uint8_t *rgb, uint16_t led_count);
void ws2812_deinit(void);

#ifndef ESP_PLATFORM
int ws2812_tim_dma_init(const ws2812_config_t *config);
int ws2812_tim_dma_send(const uint8_t *rgb, uint16_t led_count);
int ws2812_spi_dma_init(const ws2812_config_t *config);
int ws2812_spi_dma_send(const uint8_t *rgb, uint16_t led_count);
#endif
