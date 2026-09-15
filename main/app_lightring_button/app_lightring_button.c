#include <stdbool.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lightring.h"

#if !defined(BOARD_HAS_LIGHTRING_BUTTON) || !BOARD_HAS_LIGHTRING_BUTTON
#error "lightring_button requires a board light ring and button"
#endif
#if !defined(BOARD_LIGHTRING_GPIO) || !defined(BOARD_BUTTON_GPIO)
#error "lightring_button requires board light ring and button GPIO definitions"
#endif

static const char *TAG = "lightring_button";
static const uint32_t BUTTON_SCAN_PERIOD_MS = 10U;
static const uint32_t BUTTON_DEBOUNCE_MS = 30U;
static const uint32_t MODE_CHANGE_HOLD_MS = 750U;

typedef struct {
    bool sampled_pressed;
    bool stable_pressed;
    bool mode_change_started;
    TickType_t last_edge_tick;
    TickType_t press_start_tick;
} button_state_t;

static bool button_is_pressed(void)
{
    return gpio_get_level((gpio_num_t)BOARD_BUTTON_GPIO) == 0;
}

static void button_pressed(button_state_t *state)
{
    const lightring_status_t status = lightring_get_status();

    state->press_start_tick = xTaskGetTickCount();
    state->mode_change_started = false;
    if (status == LIGHTRING_STATUS_OFF) {
        if (lightring_activate(lightring_get_mode()) != 0) {
            ESP_LOGW(TAG, "Button action rejected: activate");
        }
    } else if (status == LIGHTRING_STATUS_ON) {
        const lightring_mode_t next_mode =
            (lightring_mode_t)((lightring_get_mode() + 1) % LIGHTRING_MODE_COUNT);
        state->mode_change_started = lightring_begin_mode_change(next_mode) == 0;
        if (!state->mode_change_started) {
            ESP_LOGW(TAG, "Button action rejected: begin mode change");
        }
    } else {
        ESP_LOGI(TAG, "Button press ignored while effect is running (status=%d)", status);
    }
}

static void button_released(button_state_t *state)
{
    if (!state->mode_change_started) {
        return;
    }

    const TickType_t held_ticks = xTaskGetTickCount() - state->press_start_tick;
    int result;
    if (held_ticks < pdMS_TO_TICKS(MODE_CHANGE_HOLD_MS)) {
        result = lightring_cancel_mode_change();
    } else {
        result = lightring_commit_mode_change();
    }
    if (result != 0) {
        ESP_LOGW(TAG, "Button release action rejected");
    }
    state->mode_change_started = false;
}

static void button_poll(button_state_t *state)
{
    const TickType_t now = xTaskGetTickCount();
    const bool pressed = button_is_pressed();

    if (pressed != state->sampled_pressed) {
        state->sampled_pressed = pressed;
        state->last_edge_tick = now;
    }
    if (pressed == state->stable_pressed ||
        (now - state->last_edge_tick) < pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
        return;
    }

    state->stable_pressed = pressed;
    if (pressed) {
        ESP_LOGI(TAG, "Button pressed");
        button_pressed(state);
    } else {
        const uint32_t held_ms = (uint32_t)(((uint64_t)(now - state->press_start_tick) * 1000U) /
                                            configTICK_RATE_HZ);
        ESP_LOGI(TAG, "Button released (held=%lu ms)", (unsigned long)held_ms);
        button_released(state);
    }
}

static int button_gpio_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

void app_main(void)
{
    if (button_gpio_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure button GPIO%d", BOARD_BUTTON_GPIO);
        return;
    }

    const lightring_config_t config = {
        .ws2812 = {
            .gpio_num = BOARD_LIGHTRING_GPIO,
            .led_count = LIGHTRING_LED_COUNT,
        },
        .task_stack_bytes = 4096U,
        .task_priority = 5U,
    };
    if (lightring_init(&config) != 0) {
        ESP_LOGE(TAG, "Failed to initialize light ring on GPIO%d", BOARD_LIGHTRING_GPIO);
        return;
    }

    const bool initially_pressed = button_is_pressed();
    button_state_t button = {
        .sampled_pressed = initially_pressed,
        .stable_pressed = initially_pressed,
        .last_edge_tick = xTaskGetTickCount(),
    };
    TickType_t last_wake_tick = xTaskGetTickCount();
    ESP_LOGI(TAG, "Ready: WS2812 GPIO%d, active-low button GPIO%d",
             BOARD_LIGHTRING_GPIO, BOARD_BUTTON_GPIO);

    while (true) {
        button_poll(&button);
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(BUTTON_SCAN_PERIOD_MS));
    }
}