#include "ws2812.h"

#include "stm32f4xx_hal.h"
#include <stddef.h>

#define WS2812_TIM_BITS_PER_LED  24U
#define WS2812_TIM_RESET_SLOTS   64U
#define WS2812_TIM_BUFFER_LEN    ((LIGHTRING_LED_COUNT * WS2812_TIM_BITS_PER_LED) + WS2812_TIM_RESET_SLOTS)

static TIM_HandleTypeDef *s_htim;
static uint32_t           s_channel;
static uint16_t           s_ccr0;
static uint16_t           s_ccr1;
static uint16_t           s_buffer[WS2812_TIM_BUFFER_LEN];

static uint32_t channel_to_dma_id(uint32_t channel)
{
    switch (channel) {
    case TIM_CHANNEL_2: return TIM_DMA_ID_CC2;
    case TIM_CHANNEL_3: return TIM_DMA_ID_CC3;
    case TIM_CHANNEL_4: return TIM_DMA_ID_CC4;
    case TIM_CHANNEL_1:
    default:            return TIM_DMA_ID_CC1;
    }
}

int ws2812_tim_dma_init(const ws2812_config_t *config)
{
    if (config == NULL || config->tim == NULL) {
        return -1;
    }
    s_htim = (TIM_HandleTypeDef *) config->tim;
    s_channel = config->tim_channel;

    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(s_htim) + 1U;
    s_ccr0 = (uint16_t) ((arr * 28U) / 100U);
    s_ccr1 = (uint16_t) ((arr * 56U) / 100U);
    return 0;
}

static void encode_byte(uint8_t value, uint16_t *out)
{
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        out[bit] = (value & 0x80U) ? s_ccr1 : s_ccr0;
        value = (uint8_t) (value << 1U);
    }
}

int ws2812_tim_dma_send(const uint8_t *rgb, uint16_t led_count)
{
    if (s_htim == NULL || rgb == NULL) {
        return -1;
    }
    if (led_count > LIGHTRING_LED_COUNT) {
        led_count = LIGHTRING_LED_COUNT;
    }

    uint16_t *cursor = s_buffer;
    for (uint16_t led = 0U; led < led_count; ++led) {
        const uint8_t *pixel = &rgb[led * 3U];
        encode_byte(pixel[0], cursor); cursor += 8U;
        encode_byte(pixel[1], cursor); cursor += 8U;
        encode_byte(pixel[2], cursor); cursor += 8U;
    }
    for (uint16_t slot = 0U; slot < WS2812_TIM_RESET_SLOTS; ++slot) {
        *cursor++ = 0U;
    }

    uint16_t length = (uint16_t) ((led_count * WS2812_TIM_BITS_PER_LED) + WS2812_TIM_RESET_SLOTS);
    if (HAL_TIM_PWM_Start_DMA(s_htim, s_channel, (const uint32_t *) s_buffer, length) != HAL_OK) {
        return -1;
    }

    DMA_HandleTypeDef *hdma = s_htim->hdma[channel_to_dma_id(s_channel)];
    uint32_t guard = 0U;
    while (hdma != NULL && __HAL_DMA_GET_COUNTER(hdma) != 0U) {
        if (++guard > 2000000U) {
            break;
        }
    }

    HAL_TIM_PWM_Stop_DMA(s_htim, s_channel);
    return 0;
}