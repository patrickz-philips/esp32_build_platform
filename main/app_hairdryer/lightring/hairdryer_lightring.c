#include "hairdryer_lightring.h"

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "hairdryer_lightring";

esp_err_t hairdryer_lightring_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOARD_HAIRDRYER_WS2812_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_set_level((gpio_num_t)BOARD_HAIRDRYER_WS2812_GPIO, 0);
}

void hairdryer_lightring_button_mode_click(void)
{
    ESP_LOGI(TAG, "Mode click action pending");
}

void hairdryer_lightring_button_cold_pressed(void)
{
    ESP_LOGI(TAG, "Cold press action pending");
}

void hairdryer_lightring_button_cold_released(void)
{
    ESP_LOGI(TAG, "Cold release action pending");
}

void hairdryer_lightring_button_switch_off(void)
{
    ESP_LOGI(TAG, "Switch off action pending");
}

void hairdryer_lightring_button_switch_low(void)
{
    ESP_LOGI(TAG, "Switch low action pending");
}

void hairdryer_lightring_button_switch_high(void)
{
    ESP_LOGI(TAG, "Switch high action pending");
}
