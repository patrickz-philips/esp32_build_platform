#include "board_config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if !defined(BOARD_FEATURE_HAIRDRYER) || !BOARD_FEATURE_HAIRDRYER
#error "hairdryer requires BOARD_FEATURE_HAIRDRYER"
#endif
#if !defined(BOARD_HAIRDRYER_WS2812_GPIO) || \
    !defined(BOARD_HAIRDRYER_BUTTON_1_GPIO) || \
    !defined(BOARD_HAIRDRYER_BUTTON_2_GPIO) || \
    !defined(BOARD_HAIRDRYER_BUTTON_3_GPIO) || \
    !defined(BOARD_HAIRDRYER_BUTTON_4_GPIO)
#error "hairdryer requires its WS2812 and button GPIO definitions"
#endif

static const char *TAG = "hairdryer";
static const uint32_t BUTTON_LOG_PERIOD_MS = 1000U;

static esp_err_t gpio_init(void)
{
    const gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BOARD_HAIRDRYER_BUTTON_1_GPIO) |
                        (1ULL << BOARD_HAIRDRYER_BUTTON_2_GPIO) |
                        (1ULL << BOARD_HAIRDRYER_BUTTON_3_GPIO) |
                        (1ULL << BOARD_HAIRDRYER_BUTTON_4_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&button_config);
    if (err != ESP_OK) {
        return err;
    }

    const gpio_config_t ws2812_config = {
        .pin_bit_mask = 1ULL << BOARD_HAIRDRYER_WS2812_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&ws2812_config);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_set_level((gpio_num_t)BOARD_HAIRDRYER_WS2812_GPIO, 0);
}

void app_main(void)
{
    const esp_err_t err = gpio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIOs: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Ready: WS2812 GPIO%d low; buttons GPIO%d, GPIO%d, GPIO%d, GPIO%d",
             BOARD_HAIRDRYER_WS2812_GPIO,
             BOARD_HAIRDRYER_BUTTON_1_GPIO,
             BOARD_HAIRDRYER_BUTTON_2_GPIO,
             BOARD_HAIRDRYER_BUTTON_3_GPIO,
             BOARD_HAIRDRYER_BUTTON_4_GPIO);

    TickType_t last_wake_tick = xTaskGetTickCount();
    while (true) {
        ESP_LOGI(TAG, "GPIO%d=%d GPIO%d=%d GPIO%d=%d GPIO%d=%d",
                 BOARD_HAIRDRYER_BUTTON_1_GPIO,
                 gpio_get_level((gpio_num_t)BOARD_HAIRDRYER_BUTTON_1_GPIO),
                 BOARD_HAIRDRYER_BUTTON_2_GPIO,
                 gpio_get_level((gpio_num_t)BOARD_HAIRDRYER_BUTTON_2_GPIO),
                 BOARD_HAIRDRYER_BUTTON_3_GPIO,
                 gpio_get_level((gpio_num_t)BOARD_HAIRDRYER_BUTTON_3_GPIO),
                 BOARD_HAIRDRYER_BUTTON_4_GPIO,
                 gpio_get_level((gpio_num_t)BOARD_HAIRDRYER_BUTTON_4_GPIO));
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(BUTTON_LOG_PERIOD_MS));
    }
}
