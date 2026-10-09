#include <stdbool.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lightring_button_2_effects.h"
#include "ws2812.h"

#if !defined(BOARD_HAS_LIGHTRING_BUTTON) || !BOARD_HAS_LIGHTRING_BUTTON
#error "lightring_button_2 requires a board light ring and button"
#endif
#if !defined(BOARD_LIGHTRING_GPIO) || !defined(BOARD_BUTTON_GPIO)
#error "lightring_button_2 requires board light ring and button GPIO definitions"
#endif

static const char *TAG = "lightring_button_2";
static const uint32_t BUTTON_SCAN_PERIOD_MS = 10U;
static const uint32_t BUTTON_DEBOUNCE_MS = 30U;

typedef enum {
    EFFECT_IDLE = 0,
    EFFECT_PREVIEW,
} effect_state_t;

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_PRESSED,
    BUTTON_EVENT_RELEASED,
} button_event_t;

typedef struct {
    bool sampled_pressed;
    bool stable_pressed;
    TickType_t last_edge_tick;
} button_state_t;

typedef struct {
    effect_state_t state;
    lightring_button_2_mode_t active_mode;
    lightring_button_2_mode_t target_mode;
    TickType_t started_tick;
} effect_t;

static bool button_is_pressed(void)
{
    return gpio_get_level((gpio_num_t)BOARD_BUTTON_GPIO) == 0;
}

static uint32_t ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000U) / configTICK_RATE_HZ);
}

static button_event_t button_poll(button_state_t *state, TickType_t now)
{
    const bool pressed = button_is_pressed();

    if (pressed != state->sampled_pressed) {
        state->sampled_pressed = pressed;
        state->last_edge_tick = now;
    }
    if (pressed == state->stable_pressed ||
        (now - state->last_edge_tick) < pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
        return BUTTON_EVENT_NONE;
    }

    state->stable_pressed = pressed;
    return pressed ? BUTTON_EVENT_PRESSED : BUTTON_EVENT_RELEASED;
}

static void start_preview(effect_t *effect, TickType_t now)
{
    if (effect->state != EFFECT_IDLE) {
        ESP_LOGI(TAG, "Button press ignored while preview is running");
        return;
    }

    effect->target_mode =
        (lightring_button_2_mode_t)((effect->active_mode + 1) %
                                    LIGHTRING_BUTTON_2_MODE_COUNT);
    effect->started_tick = now;
    effect->state = EFFECT_PREVIEW;
    ESP_LOGI(TAG, "Preview started: target mode=%d", effect->target_mode);
}

static bool handle_release(effect_t *effect, TickType_t now)
{
    if (effect->state != EFFECT_PREVIEW) {
        return false;
    }

    const uint32_t held_ms = ticks_to_ms(now - effect->started_tick);
    if (lightring_button_2_decide(held_ms, true) ==
        LIGHTRING_BUTTON_2_DECISION_RESTORE) {
        ESP_LOGI(TAG, "Short press: restoring mode=%d", effect->active_mode);
    } else {
        effect->active_mode = effect->target_mode;
        ESP_LOGI(TAG, "Long press: applying mode=%d", effect->active_mode);
    }

    lightring_button_2_render_solid(effect->active_mode);
    effect->state = EFFECT_IDLE;
    return true;
}

static bool render_effect(effect_t *effect, TickType_t now)
{
    if (effect->state != EFFECT_PREVIEW) {
        return false;
    }

    const uint32_t elapsed_ms = ticks_to_ms(now - effect->started_tick);
    if (lightring_button_2_decide(elapsed_ms, false) ==
        LIGHTRING_BUTTON_2_DECISION_COMMIT) {
        effect->active_mode = effect->target_mode;
        effect->state = EFFECT_IDLE;
        lightring_button_2_render_solid(effect->active_mode);
        ESP_LOGI(TAG, "Hold threshold reached: active mode=%d", effect->active_mode);
    } else {
        lightring_button_2_render_preview(elapsed_ms);
    }
    return true;
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

    const ws2812_config_t config = {
        .gpio_num = BOARD_LIGHTRING_GPIO,
        .led_count = LIGHTRING_LED_COUNT,
    };
    if (ws2812_init(&config) != 0) {
        ESP_LOGE(TAG, "Failed to initialize light ring on GPIO%d", BOARD_LIGHTRING_GPIO);
        return;
    }

    lightring_button_2_render_solid(LIGHTRING_BUTTON_2_MODE_RED);
    if (ws2812_send(lightring_button_2_framebuffer, LIGHTRING_LED_COUNT) != 0) {
        ESP_LOGE(TAG, "Failed to display startup color");
        ws2812_deinit();
        return;
    }

    const bool initially_pressed = button_is_pressed();
    button_state_t button = {
        .sampled_pressed = initially_pressed,
        .stable_pressed = initially_pressed,
        .last_edge_tick = xTaskGetTickCount(),
    };
    effect_t effect = {
        .state = EFFECT_IDLE,
        .active_mode = LIGHTRING_BUTTON_2_MODE_RED,
        .target_mode = LIGHTRING_BUTTON_2_MODE_RED,
    };
    TickType_t last_wake_tick = xTaskGetTickCount();
    ESP_LOGI(TAG, "Ready: red mode, WS2812 GPIO%d, active-low button GPIO%d",
             BOARD_LIGHTRING_GPIO, BOARD_BUTTON_GPIO);

    while (true) {
        const TickType_t now = xTaskGetTickCount();
        const button_event_t event = button_poll(&button, now);
        bool frame_changed = false;
        if (event == BUTTON_EVENT_PRESSED) {
            start_preview(&effect, now);
        } else if (event == BUTTON_EVENT_RELEASED) {
            frame_changed = handle_release(&effect, now);
        }

        frame_changed = render_effect(&effect, now) || frame_changed;
        if (frame_changed &&
            ws2812_send(lightring_button_2_framebuffer, LIGHTRING_LED_COUNT) != 0) {
            ESP_LOGE(TAG, "Light ring output failed");
            ws2812_deinit();
            return;
        }
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(BUTTON_SCAN_PERIOD_MS));
    }
}
