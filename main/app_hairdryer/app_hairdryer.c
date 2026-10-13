#include <stdbool.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hairdryer_buttons.h"
#include "lightring.h"

#if !defined(BOARD_FEATURE_HAIRDRYER) || !BOARD_FEATURE_HAIRDRYER
#error "hairdryer requires BOARD_FEATURE_HAIRDRYER"
#endif
#if !defined(BOARD_HAIRDRYER_WS2812_GPIO) || \
    !defined(BOARD_HAIRDRYER_BUTTON_MODE_GPIO) || \
    !defined(BOARD_HAIRDRYER_BUTTON_COLD_GPIO) || \
    !defined(BOARD_HAIRDRYER_BUTTON_SWITCH_OFF_GPIO) || \
    !defined(BOARD_HAIRDRYER_BUTTON_SWITCH_HIGH_GPIO)
#error "hairdryer requires its WS2812 and button GPIO definitions"
#endif

static const char *TAG = "hairdryer";
static const uint32_t BUTTON_SCAN_PERIOD_MS = 10U;
static const UBaseType_t BUTTON_EVENT_QUEUE_LENGTH = 8U;
static const uint32_t BUTTON_TASK_STACK_SIZE = 3072U;
static const uint32_t DISPATCH_TASK_STACK_SIZE = 3072U;
static const UBaseType_t BUTTON_TASK_PRIORITY = 3U;
static const UBaseType_t DISPATCH_TASK_PRIORITY = 4U;

static QueueHandle_t s_button_event_queue;
static TaskHandle_t s_button_task;
static TaskHandle_t s_dispatch_task;

static esp_err_t button_gpio_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BOARD_HAIRDRYER_BUTTON_MODE_GPIO) |
                        (1ULL << BOARD_HAIRDRYER_BUTTON_COLD_GPIO) |
                        (1ULL << BOARD_HAIRDRYER_BUTTON_SWITCH_OFF_GPIO) |
                        (1ULL << BOARD_HAIRDRYER_BUTTON_SWITCH_HIGH_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static hairdryer_button_levels_t read_button_levels(void)
{
    return (hairdryer_button_levels_t) {
        .mode = (uint8_t)gpio_get_level((gpio_num_t)BOARD_HAIRDRYER_BUTTON_MODE_GPIO),
        .cold = (uint8_t)gpio_get_level((gpio_num_t)BOARD_HAIRDRYER_BUTTON_COLD_GPIO),
        .switch_off =
            (uint8_t)gpio_get_level((gpio_num_t)BOARD_HAIRDRYER_BUTTON_SWITCH_OFF_GPIO),
        .switch_high =
            (uint8_t)gpio_get_level((gpio_num_t)BOARD_HAIRDRYER_BUTTON_SWITCH_HIGH_GPIO),
    };
}

static uint32_t ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000U) / configTICK_RATE_HZ);
}

static void button_task(void *arg)
{
    (void)arg;

    hairdryer_buttons_t buttons;
    hairdryer_buttons_init(&buttons, read_button_levels(), ticks_to_ms(xTaskGetTickCount()));

    TickType_t last_wake_tick = xTaskGetTickCount();
    while (true) {
        hairdryer_button_event_t events[HAIRDRYER_BUTTON_EVENT_COUNT];
        const size_t event_count =
            hairdryer_buttons_poll(&buttons, read_button_levels(),
                                   ticks_to_ms(xTaskGetTickCount()), events);

        for (size_t i = 0; i < event_count; ++i) {
            if (xQueueSendToBack(s_button_event_queue, &events[i], 0) != pdTRUE) {
                ESP_LOGW(TAG, "Button event queue full; event source=%d dropped",
                         events[i].source);
            }
        }
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(BUTTON_SCAN_PERIOD_MS));
    }
}

static void dispatch_button_event(const hairdryer_button_event_t *event)
{
    int result = -1;

    switch (event->source) {
    case HAIRDRYER_BUTTON_SOURCE_MODE:
        ESP_LOGI(TAG, "BUTTON_MODE_CLICK");
        result = lightring_change_mode(
            (lightring_mode_t)((lightring_get_mode() + 1) % LIGHTRING_MODE_COUNT));
        break;
    case HAIRDRYER_BUTTON_SOURCE_COLD:
        if (event->value.cold == HAIRDRYER_BUTTON_COLD_EVENT_PRESSED) {
            ESP_LOGI(TAG, "BUTTON_COLD_PRESSED");
            result = lightring_cold_on();
        } else {
            ESP_LOGI(TAG, "BUTTON_COLD_RELEASED");
            result = lightring_cold_off();
        }
        break;
    case HAIRDRYER_BUTTON_SOURCE_SWITCH:
        if (event->value.switch_state == HAIRDRYER_BUTTON_SWITCH_OFF) {
            ESP_LOGI(TAG, "BUTTON_SWITCH_OFF");
            result = lightring_change_speed(LIGHTRING_SPEED_OFF);
        } else if (event->value.switch_state == HAIRDRYER_BUTTON_SWITCH_LOW) {
            ESP_LOGI(TAG, "BUTTON_SWITCH_LOW");
            result = lightring_change_speed(LIGHTRING_SPEED_LOW);
        } else {
            ESP_LOGI(TAG, "BUTTON_SWITCH_HIGH");
            result = lightring_change_speed(LIGHTRING_SPEED_HIGH);
        }
        break;
    }

    if (result != 0) {
        ESP_LOGW(TAG, "Light ring rejected button event source=%d status=%d",
                 event->source, lightring_get_status());
    }
}

static void dispatch_task(void *arg)
{
    (void)arg;

    hairdryer_button_event_t event;
    while (true) {
        if (xQueueReceive(s_button_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            dispatch_button_event(&event);
        }
    }
}

static void runtime_stop(void)
{
    if (s_button_task != NULL) {
        vTaskDelete(s_button_task);
        s_button_task = NULL;
    }
    if (s_dispatch_task != NULL) {
        vTaskDelete(s_dispatch_task);
        s_dispatch_task = NULL;
    }
    if (s_button_event_queue != NULL) {
        vQueueDelete(s_button_event_queue);
        s_button_event_queue = NULL;
    }
    lightring_deinit();
}

static esp_err_t runtime_start(void)
{
    esp_err_t err = button_gpio_init();
    if (err != ESP_OK) {
        return err;
    }

    s_button_event_queue =
        xQueueCreate(BUTTON_EVENT_QUEUE_LENGTH, sizeof(hairdryer_button_event_t));
    if (s_button_event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const hairdryer_button_levels_t levels = read_button_levels();
    hairdryer_button_switch_state_t switch_state;
    const bool switch_valid = hairdryer_button_switch_decode(levels, &switch_state);
    const lightring_config_t lightring_config = {
        .ws2812 = {
            .gpio_num = BOARD_HAIRDRYER_WS2812_GPIO,
            .led_count = LIGHTRING_LED_COUNT,
        },
        .initial_mode = LIGHTRING_MODE_THERMO,
        .initial_speed =
            !switch_valid || switch_state == HAIRDRYER_BUTTON_SWITCH_OFF
                ? LIGHTRING_SPEED_OFF
                : (switch_state == HAIRDRYER_BUTTON_SWITCH_HIGH
                       ? LIGHTRING_SPEED_HIGH
                       : LIGHTRING_SPEED_LOW),
        .task_stack_bytes = 3072U,
        .task_priority = 5U,
    };
    if (lightring_init(&lightring_config) != 0) {
        runtime_stop();
        return ESP_FAIL;
    }

    if (xTaskCreate(dispatch_task, "hairdryer_dispatch", DISPATCH_TASK_STACK_SIZE, NULL,
                    DISPATCH_TASK_PRIORITY, &s_dispatch_task) != pdPASS ||
        xTaskCreate(button_task, "hairdryer_buttons", BUTTON_TASK_STACK_SIZE, NULL,
                    BUTTON_TASK_PRIORITY, &s_button_task) != pdPASS) {
        runtime_stop();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_main(void)
{
    const esp_err_t err = runtime_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Hairdryer runtime: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG,
             "Ready: mode GPIO%d, cold GPIO%d, switch GPIO%d/GPIO%d, WS2812 GPIO%d",
             BOARD_HAIRDRYER_BUTTON_MODE_GPIO,
             BOARD_HAIRDRYER_BUTTON_COLD_GPIO,
             BOARD_HAIRDRYER_BUTTON_SWITCH_OFF_GPIO,
             BOARD_HAIRDRYER_BUTTON_SWITCH_HIGH_GPIO,
             BOARD_HAIRDRYER_WS2812_GPIO);
}
