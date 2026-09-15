#include <stddef.h>
#include <stdlib.h>

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "ws2812.h"

#define WS2812_RESOLUTION_HZ      10000000U
#define WS2812_MEM_BLOCK_SYMBOLS  128U

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_handle_t bytes_encoder;
    rmt_encoder_handle_t reset_encoder;
    rmt_symbol_word_t reset_symbol;
    int phase;
} ws2812_encoder_t;

static const char *TAG = "ws2812";
static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static uint16_t s_led_count;
static uint8_t s_grb_pixels[LIGHTRING_LED_COUNT * 3U];

static size_t ws2812_encode(rmt_encoder_t *encoder,
                            rmt_channel_handle_t channel,
                            const void *primary_data,
                            size_t data_size,
                            rmt_encode_state_t *ret_state)
{
    ws2812_encoder_t *strip_encoder = __containerof(encoder, ws2812_encoder_t, base);
    rmt_encode_state_t encode_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0U;

    if (strip_encoder->phase == 0) {
        encoded_symbols += strip_encoder->bytes_encoder->encode(
            strip_encoder->bytes_encoder, channel, primary_data, data_size, &encode_state);
        if (encode_state & RMT_ENCODING_COMPLETE) {
            strip_encoder->phase = 1;
        }
        if (encode_state & RMT_ENCODING_MEM_FULL) {
            *ret_state = RMT_ENCODING_MEM_FULL;
            return encoded_symbols;
        }
    }

    encoded_symbols += strip_encoder->reset_encoder->encode(
        strip_encoder->reset_encoder, channel, &strip_encoder->reset_symbol,
        sizeof(strip_encoder->reset_symbol), &encode_state);
    if (encode_state & RMT_ENCODING_COMPLETE) {
        strip_encoder->phase = 0;
        state |= RMT_ENCODING_COMPLETE;
    }
    if (encode_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
    }
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t ws2812_encoder_del(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *strip_encoder = __containerof(encoder, ws2812_encoder_t, base);
    esp_err_t result = ESP_OK;

    if (strip_encoder->bytes_encoder != NULL) {
        result = rmt_del_encoder(strip_encoder->bytes_encoder);
    }
    if (strip_encoder->reset_encoder != NULL) {
        esp_err_t ret = rmt_del_encoder(strip_encoder->reset_encoder);
        if (result == ESP_OK) {
            result = ret;
        }
    }
    free(strip_encoder);
    return result;
}

static esp_err_t ws2812_encoder_reset(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *strip_encoder = __containerof(encoder, ws2812_encoder_t, base);

    ESP_RETURN_ON_ERROR(rmt_encoder_reset(strip_encoder->bytes_encoder), TAG,
                        "Failed to reset bytes encoder");
    ESP_RETURN_ON_ERROR(rmt_encoder_reset(strip_encoder->reset_encoder), TAG,
                        "Failed to reset copy encoder");
    strip_encoder->phase = 0;
    return ESP_OK;
}

static esp_err_t ws2812_encoder_new(rmt_encoder_handle_t *ret_encoder)
{
    ws2812_encoder_t *strip_encoder = rmt_alloc_encoder_mem(sizeof(ws2812_encoder_t));
    ESP_RETURN_ON_FALSE(strip_encoder != NULL, ESP_ERR_NO_MEM, TAG,
                        "Failed to allocate encoder");

    strip_encoder->base.encode = ws2812_encode;
    strip_encoder->base.del = ws2812_encoder_del;
    strip_encoder->base.reset = ws2812_encoder_reset;
    strip_encoder->phase = 0;

    const rmt_bytes_encoder_config_t bytes_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = WS2812_RESOLUTION_HZ / 3333333U,
            .level1 = 0,
            .duration1 = (WS2812_RESOLUTION_HZ * 9U) / 10000000U,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = (WS2812_RESOLUTION_HZ * 9U) / 10000000U,
            .level1 = 0,
            .duration1 = WS2812_RESOLUTION_HZ / 3333333U,
        },
        .flags.msb_first = 1,
    };
    esp_err_t ret = rmt_new_bytes_encoder(&bytes_config, &strip_encoder->bytes_encoder);
    if (ret != ESP_OK) {
        free(strip_encoder);
        return ret;
    }

    const rmt_copy_encoder_config_t copy_config = {};
    ret = rmt_new_copy_encoder(&copy_config, &strip_encoder->reset_encoder);
    if (ret != ESP_OK) {
        (void)rmt_del_encoder(strip_encoder->bytes_encoder);
        free(strip_encoder);
        return ret;
    }

    const uint32_t reset_ticks = (WS2812_RESOLUTION_HZ / 1000000U) * 25U;
    strip_encoder->reset_symbol = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = reset_ticks,
        .level1 = 0,
        .duration1 = reset_ticks,
    };
    *ret_encoder = &strip_encoder->base;
    return ESP_OK;
}

int ws2812_init(const ws2812_config_t *config)
{
    if (config == NULL || config->gpio_num < 0 || config->led_count == 0U ||
        config->led_count > LIGHTRING_LED_COUNT ||
        s_channel != NULL) {
        return -1;
    }

    const rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = config->gpio_num,
        .mem_block_symbols = WS2812_MEM_BLOCK_SYMBOLS,
        .resolution_hz = WS2812_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    if (rmt_new_tx_channel(&channel_config, &s_channel) != ESP_OK) {
        return -1;
    }
    if (ws2812_encoder_new(&s_encoder) != ESP_OK) {
        (void)rmt_del_channel(s_channel);
        s_channel = NULL;
        return -1;
    }
    if (rmt_enable(s_channel) != ESP_OK) {
        (void)rmt_del_encoder(s_encoder);
        (void)rmt_del_channel(s_channel);
        s_encoder = NULL;
        s_channel = NULL;
        return -1;
    }
    s_led_count = config->led_count;
    ESP_LOGI(TAG, "RMT ready on GPIO%d, GRB wire order", config->gpio_num);
    return 0;
}

int ws2812_send(const uint8_t *rgb, uint16_t led_count)
{
    if (s_channel == NULL || s_encoder == NULL || rgb == NULL ||
        led_count != s_led_count) {
        return -1;
    }

    const rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    const size_t data_size = (size_t)led_count * 3U;
    for (uint16_t led = 0U; led < led_count; ++led) {
        const size_t offset = (size_t)led * 3U;
        s_grb_pixels[offset] = rgb[offset + 1U];
        s_grb_pixels[offset + 1U] = rgb[offset];
        s_grb_pixels[offset + 2U] = rgb[offset + 2U];
    }
    if (rmt_transmit(s_channel, s_encoder, s_grb_pixels, data_size, &tx_config) != ESP_OK) {
        return -1;
    }
    return rmt_tx_wait_all_done(s_channel, portMAX_DELAY) == ESP_OK ? 0 : -1;
}

void ws2812_deinit(void)
{
    if (s_channel == NULL) {
        return;
    }
    if (rmt_disable(s_channel) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to disable RMT channel");
    }
    if (s_encoder != NULL && rmt_del_encoder(s_encoder) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to release RMT encoder");
    }
    if (rmt_del_channel(s_channel) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to release RMT channel");
    }
    s_encoder = NULL;
    s_channel = NULL;
    s_led_count = 0U;
}