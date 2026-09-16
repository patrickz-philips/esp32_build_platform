#include "ws2812.h"

#include "stm32f4xx_hal.h"
#include <string.h>

#define WS2812_SPI_TICKS_PER_BIT       25U
#define WS2812_SPI_0_HIGH_TICKS         7U
#define WS2812_SPI_1_HIGH_TICKS        19U
#define WS2812_SPI_BYTES_PER_LED       ((24U * WS2812_SPI_TICKS_PER_BIT) / 8U)
#define WS2812_SPI_RESET_BYTES         727U
#define WS2812_SPI_BUFFER_LEN          ((LIGHTRING_LED_COUNT * WS2812_SPI_BYTES_PER_LED) + WS2812_SPI_RESET_BYTES)

static SPI_HandleTypeDef *s_hspi;
static uint8_t            s_buffer[WS2812_SPI_BUFFER_LEN];

int ws2812_spi_dma_init(const ws2812_config_t *config)
{
    if (config == NULL || config->spi == NULL) {
        return -1;
    }
    s_hspi = (SPI_HandleTypeDef *) config->spi;

    s_hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    return (HAL_SPI_Init(s_hspi) == HAL_OK) ? 0 : -1;
}

static void write_high_ticks(uint32_t *bit_offset, uint8_t high_ticks)
{
    for (uint8_t tick = 0U; tick < high_ticks; ++tick) {
        uint32_t offset = *bit_offset + tick;
        s_buffer[offset / 8U] |= (uint8_t) (0x80U >> (offset % 8U));
    }
    *bit_offset += WS2812_SPI_TICKS_PER_BIT;
}

static void encode_byte(uint8_t value, uint32_t *bit_offset)
{
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        uint8_t high_ticks = (value & 0x80U) ? WS2812_SPI_1_HIGH_TICKS
                                             : WS2812_SPI_0_HIGH_TICKS;
        write_high_ticks(bit_offset, high_ticks);
        value = (uint8_t) (value << 1U);
    }
}

int ws2812_spi_dma_send(const uint8_t *rgb, uint16_t led_count)
{
    if (s_hspi == NULL || rgb == NULL) {
        return -1;
    }
    if (led_count > LIGHTRING_LED_COUNT) {
        led_count = LIGHTRING_LED_COUNT;
    }

    uint16_t data_length = (uint16_t) (led_count * WS2812_SPI_BYTES_PER_LED);
    uint16_t length = (uint16_t) (data_length + WS2812_SPI_RESET_BYTES);
    memset(s_buffer, 0, length);

    uint32_t bit_offset = 0U;
    for (uint16_t led = 0U; led < led_count; ++led) {
        const uint8_t *pixel = &rgb[led * 3U];
        encode_byte(pixel[0], &bit_offset);
        encode_byte(pixel[1], &bit_offset);
        encode_byte(pixel[2], &bit_offset);
    }

    if (HAL_SPI_Transmit_DMA(s_hspi, s_buffer, length) != HAL_OK) {
        return -1;
    }

    uint32_t guard = 0U;
    while (s_hspi->State != HAL_SPI_STATE_READY) {
        if (++guard > 2000000U) {
            (void) HAL_SPI_Abort(s_hspi);
            return -1;
        }
    }
    return 0;
}