#include "ws2812.h"

#include <stddef.h>

static lightring_ws2812_backend_t s_backend = LIGHTRING_WS2812_TIM_DMA;

int ws2812_init(const ws2812_config_t *config)
{
    if (config == NULL) {
        return -1;
    }
    s_backend = config->backend;

    if (s_backend == LIGHTRING_WS2812_SPI_DMA) {
        return ws2812_spi_dma_init(config);
    }
    return ws2812_tim_dma_init(config);
}

int ws2812_send(const uint8_t *rgb, uint16_t led_count)
{
    if (s_backend == LIGHTRING_WS2812_SPI_DMA) {
        return ws2812_spi_dma_send(rgb, led_count);
    }
    return ws2812_tim_dma_send(rgb, led_count);
}

void ws2812_deinit(void)
{
}