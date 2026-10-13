#include <stdbool.h>
#include <math.h>
#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include "FreeRTOS.h"
#include "task.h"
#endif

#include "lightring.h"
#include "lightring_effects.h"
#include "ws2812.h"

#ifdef ESP_PLATFORM
#define LR_DEFAULT_STACK_SIZE 3072U
#define LR_ENTER_CRITICAL() taskENTER_CRITICAL(&s_lock)
#define LR_EXIT_CRITICAL() taskEXIT_CRITICAL(&s_lock)
#define LR_LOG_ERROR(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
#define LR_DEFAULT_STACK_SIZE 512U
#define LR_ENTER_CRITICAL() taskENTER_CRITICAL()
#define LR_EXIT_CRITICAL() taskEXIT_CRITICAL()
#define LR_LOG_ERROR(...) ((void)0)
#endif
#define LR_DEFAULT_PRIORITY 5U

typedef enum {
    LR_TRANSITION_NONE = 0,
    LR_TRANSITION_MODE,
    LR_TRANSITION_SPEED_TO_LOW,
    LR_TRANSITION_SPEED_WAIT_HIGH,
    LR_TRANSITION_SPEED_TO_HIGH,
    LR_TRANSITION_SPEED_TO_OFF,
    LR_TRANSITION_OFF_TO_ACTIVE,
    LR_TRANSITION_COLD,
} lr_transition_t;

static TaskHandle_t                 s_task;
static volatile lightring_mode_t   s_mode = LIGHTRING_MODE_THERMO;
static volatile lightring_mode_t   s_target_mode = LIGHTRING_MODE_THERMO;
static volatile lightring_speed_t  s_speed = LIGHTRING_SPEED_HIGH;
static volatile lightring_speed_t  s_target_speed = LIGHTRING_SPEED_HIGH;
static volatile lr_transition_t     s_transition;
static volatile bool                s_cold;
static volatile uint8_t             s_cold_mix;
static volatile bool                s_output_failed;
static bool                         s_target_cold;
static uint8_t                      s_cold_from_mix;
static uint32_t                     s_cold_generation;
static TickType_t                   s_transition_started;
static TickType_t                   s_phase_started;
static uint32_t                     s_phase_base_ms;
static uint32_t                     s_speed_from_phase_ms;
#ifdef ESP_PLATFORM
static const char                  *TAG = "lightring";
static portMUX_TYPE                 s_lock = portMUX_INITIALIZER_UNLOCKED;
#endif

static uint32_t ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000U) / configTICK_RATE_HZ);
}

static bool valid_mode(lightring_mode_t mode)
{
    return mode >= LIGHTRING_MODE_THERMO && mode < LIGHTRING_MODE_COUNT;
}

static bool valid_speed(lightring_speed_t speed)
{
    return speed >= LIGHTRING_SPEED_OFF && speed < LIGHTRING_SPEED_COUNT;
}

static uint32_t current_phase_ms(TickType_t now)
{
    return (s_phase_base_ms + ticks_to_ms(now - s_phase_started)) %
           LIGHTRING_EFFECT_PERIOD_MS;
}

static uint8_t transition_mix(TickType_t now)
{
    uint32_t elapsed = ticks_to_ms(now - s_transition_started);
    if (elapsed >= LIGHTRING_TRANSITION_MS) {
        return 255U;
    }
    return (uint8_t)((elapsed * 255U) / LIGHTRING_TRANSITION_MS);
}

static uint8_t mix8(uint8_t from, uint8_t to, uint8_t amount)
{
    return (uint8_t)((((uint16_t)from * (uint16_t)(255U - amount)) +
                      ((uint16_t)to * amount) + 127U) / 255U);
}

static uint8_t breathe_mix(TickType_t now)
{
    uint32_t elapsed = ticks_to_ms(now - s_transition_started);
    if (elapsed >= LIGHTRING_TRANSITION_MS) {
        return 255U;
    }
    float progress = (float)elapsed / (float)LIGHTRING_TRANSITION_MS;
    return (uint8_t)((0.5f - 0.5f * cosf(3.14159265f * progress)) * 255.0f + 0.5f);
}

static lightring_status_t status_for_transition(lr_transition_t transition)
{
    switch (transition) {
    case LR_TRANSITION_MODE:
        return LIGHTRING_STATUS_MODE_CHANGING;
    case LR_TRANSITION_SPEED_TO_LOW:
        return LIGHTRING_STATUS_SPEED_COLLAPSING;
    case LR_TRANSITION_SPEED_WAIT_HIGH:
        return LIGHTRING_STATUS_SPEED_WAITING_MAX;
    case LR_TRANSITION_SPEED_TO_HIGH:
        return LIGHTRING_STATUS_SPEED_EXPANDING;
    case LR_TRANSITION_SPEED_TO_OFF:
        return LIGHTRING_STATUS_DEACTIVATING;
    case LR_TRANSITION_OFF_TO_ACTIVE:
        return LIGHTRING_STATUS_ACTIVATING;
    case LR_TRANSITION_COLD:
        return LIGHTRING_STATUS_COLD_CHANGING;
    case LR_TRANSITION_NONE:
    default:
        return LIGHTRING_STATUS_IDLE;
    }
}

static void complete_transition(TickType_t now, lr_transition_t transition)
{
    LR_ENTER_CRITICAL();
    if (s_transition != transition) {
        LR_EXIT_CRITICAL();
        return;
    }

    if (transition == LR_TRANSITION_MODE) {
        s_mode = s_target_mode;
    } else if (transition == LR_TRANSITION_SPEED_TO_LOW) {
        s_speed = LIGHTRING_SPEED_LOW;
    } else if (transition == LR_TRANSITION_SPEED_TO_HIGH) {
        s_speed = LIGHTRING_SPEED_HIGH;
        s_phase_base_ms = LIGHTRING_TRANSITION_MS;
        s_phase_started = now;
    } else if (transition == LR_TRANSITION_SPEED_TO_OFF) {
        s_speed = LIGHTRING_SPEED_OFF;
    } else if (transition == LR_TRANSITION_OFF_TO_ACTIVE) {
        s_speed = s_target_speed;
        s_phase_base_ms = 1000U;
        s_phase_started = now;
    }
    s_transition = LR_TRANSITION_NONE;
    LR_EXIT_CRITICAL();
}

static void begin_high_transition(TickType_t now, uint32_t phase_ms)
{
    LR_ENTER_CRITICAL();
    if (s_transition == LR_TRANSITION_SPEED_WAIT_HIGH) {
        s_speed_from_phase_ms = phase_ms;
        s_transition_started = now;
        s_transition = LR_TRANSITION_SPEED_TO_HIGH;
    }
    LR_EXIT_CRITICAL();
}

static void render_current_frame(TickType_t now)
{
    lightring_frame_t frame;
    lr_transition_t transition;
    uint32_t phase_ms;
    bool target_cold;
    uint8_t cold_from_mix;
    uint32_t cold_generation;
    TickType_t cold_started;

    LR_ENTER_CRITICAL();
    transition = s_transition;
    phase_ms = current_phase_ms(now);
    target_cold = s_target_cold;
    cold_from_mix = s_cold_from_mix;
    cold_generation = s_cold_generation;
    cold_started = s_transition_started;
    frame = (lightring_frame_t) {
        .from_phase_ms = phase_ms,
        .to_phase_ms = phase_ms,
        .from_mode = s_mode,
        .to_mode = s_mode,
        .mode_mix = 0U,
        .from_speed = s_speed,
        .to_speed = s_speed,
        .speed_mix = 0U,
        .cold_mix = s_cold_mix,
    };
    LR_EXIT_CRITICAL();

    if (transition == LR_TRANSITION_SPEED_WAIT_HIGH) {
        if (phase_ms >= 1000U && phase_ms < 1900U) {
            begin_high_transition(now, phase_ms);
        }
    } else if (transition == LR_TRANSITION_MODE) {
        frame.to_mode = s_target_mode;
        frame.mode_mix = transition_mix(now);
        if (frame.mode_mix == 255U) {
            complete_transition(now, transition);
        }
    } else if (transition == LR_TRANSITION_SPEED_TO_LOW) {
        frame.to_speed = LIGHTRING_SPEED_LOW;
        frame.speed_mix = transition_mix(now);
        if (frame.speed_mix == 255U) {
            complete_transition(now, transition);
        }
    } else if (transition == LR_TRANSITION_SPEED_TO_HIGH) {
        uint32_t elapsed = ticks_to_ms(now - s_transition_started);
        frame.from_phase_ms = s_speed_from_phase_ms;
        frame.to_phase_ms = elapsed;
        frame.to_speed = LIGHTRING_SPEED_HIGH;
        frame.speed_mix = transition_mix(now);
        if (frame.speed_mix == 255U) {
            complete_transition(now, transition);
        }
    } else if (transition == LR_TRANSITION_SPEED_TO_OFF) {
        frame.to_speed = LIGHTRING_SPEED_OFF;
        frame.speed_mix = breathe_mix(now);
        if (frame.speed_mix == 255U) {
            complete_transition(now, transition);
        }
    } else if (transition == LR_TRANSITION_OFF_TO_ACTIVE) {
        frame.from_phase_ms = 1000U;
        frame.to_phase_ms = 1000U;
        frame.to_speed = s_target_speed;
        frame.speed_mix = breathe_mix(now);
        if (frame.speed_mix == 255U) {
            complete_transition(now, transition);
        }
    } else if (transition == LR_TRANSITION_COLD) {
        uint32_t elapsed = ticks_to_ms(now - cold_started);
        uint8_t mix = elapsed >= LIGHTRING_TRANSITION_MS
                          ? 255U
                          : (uint8_t)((elapsed * 255U) / LIGHTRING_TRANSITION_MS);
        frame.cold_mix = mix8(cold_from_mix, target_cold ? 255U : 0U, mix);
        LR_ENTER_CRITICAL();
        if (s_transition == transition && s_cold_generation == cold_generation) {
            s_cold_mix = frame.cold_mix;
            if (mix == 255U) {
                s_cold = target_cold;
                s_transition = LR_TRANSITION_NONE;
            }
        }
        LR_EXIT_CRITICAL();
    }

    lightring_render_frame(&frame);
}

static void render_task(void *argument)
{
    (void)argument;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        render_current_frame(now);
        if (ws2812_send(lightring_framebuffer, LIGHTRING_LED_COUNT) != 0) {
            LR_LOG_ERROR("Effect output failed");
            s_output_failed = true;
        }
        vTaskDelay(pdMS_TO_TICKS(LIGHTRING_FRAME_MS));
    }
}

int lightring_init(const lightring_config_t *config)
{
    if (config == NULL || s_task != NULL ||
        !valid_mode(config->initial_mode) ||
        !valid_speed(config->initial_speed) ||
        config->ws2812.led_count != LIGHTRING_LED_COUNT) {
        return -1;
    }
    if (ws2812_init(&config->ws2812) != 0) {
        return -1;
    }

    s_mode = config->initial_mode;
    s_target_mode = config->initial_mode;
    s_speed = config->initial_speed;
    s_target_speed = config->initial_speed;
    s_transition = LR_TRANSITION_NONE;
    s_cold = false;
    s_cold_mix = 0U;
    s_cold_from_mix = 0U;
    s_cold_generation = 0U;
    s_output_failed = false;
    s_phase_base_ms = 0U;
    s_phase_started = xTaskGetTickCount();
    render_current_frame(s_phase_started);
    if (ws2812_send(lightring_framebuffer, LIGHTRING_LED_COUNT) != 0) {
        ws2812_deinit();
        return -1;
    }

#ifdef ESP_PLATFORM
    uint32_t stack = config->task_stack_bytes ? config->task_stack_bytes
                                              : LR_DEFAULT_STACK_SIZE;
#else
    uint16_t stack = config->task_stack_words ? config->task_stack_words
                                              : (uint16_t)LR_DEFAULT_STACK_SIZE;
#endif
    UBaseType_t priority = config->task_priority ? (UBaseType_t)config->task_priority
                                                 : (UBaseType_t)LR_DEFAULT_PRIORITY;
    if (xTaskCreate(render_task, "lightring", stack, NULL, priority, &s_task) != pdPASS) {
        s_task = NULL;
        ws2812_deinit();
        return -1;
    }
    return 0;
}

void lightring_deinit(void)
{
    TaskHandle_t task;

    LR_ENTER_CRITICAL();
    task = s_task;
    s_task = NULL;
    s_transition = LR_TRANSITION_NONE;
    LR_EXIT_CRITICAL();

    if (task != NULL) {
        vTaskDelete(task);
        ws2812_deinit();
    }
}

int lightring_change_mode(lightring_mode_t mode)
{
    LR_ENTER_CRITICAL();
    if (s_task == NULL || s_output_failed || !valid_mode(mode) ||
        mode == s_mode || s_transition != LR_TRANSITION_NONE) {
        LR_EXIT_CRITICAL();
        return -1;
    }
    s_target_mode = mode;
    s_transition_started = xTaskGetTickCount();
    s_transition = LR_TRANSITION_MODE;
    LR_EXIT_CRITICAL();
    return 0;
}

int lightring_change_speed(lightring_speed_t speed)
{
    LR_ENTER_CRITICAL();
    if (s_task == NULL || s_output_failed || !valid_speed(speed) ||
        speed == s_speed || s_transition != LR_TRANSITION_NONE) {
        LR_EXIT_CRITICAL();
        return -1;
    }
    TickType_t now = xTaskGetTickCount();
    s_target_speed = speed;
    s_transition_started = now;
    if (speed == LIGHTRING_SPEED_OFF) {
        s_speed = LIGHTRING_SPEED_LOW;
        s_phase_base_ms = 1000U;
        s_phase_started = now;
        s_transition = LR_TRANSITION_SPEED_TO_OFF;
    } else if (s_speed == LIGHTRING_SPEED_OFF) {
        s_transition = LR_TRANSITION_OFF_TO_ACTIVE;
    } else if (speed == LIGHTRING_SPEED_LOW) {
        s_transition = LR_TRANSITION_SPEED_TO_LOW;
    } else {
        s_transition = LR_TRANSITION_SPEED_WAIT_HIGH;
    }
    LR_EXIT_CRITICAL();
    return 0;
}

static int set_cold(bool enabled)
{
    LR_ENTER_CRITICAL();
    if (s_task == NULL || s_output_failed ||
        (s_transition != LR_TRANSITION_NONE &&
         s_transition != LR_TRANSITION_COLD) ||
        (s_transition == LR_TRANSITION_COLD && enabled == s_target_cold) ||
        (s_transition == LR_TRANSITION_NONE && enabled == s_cold)) {
        LR_EXIT_CRITICAL();
        return -1;
    }
    s_cold_from_mix = s_cold_mix;
    s_target_cold = enabled;
    ++s_cold_generation;
    if (s_cold_from_mix == (enabled ? 255U : 0U)) {
        s_cold = enabled;
        s_transition = LR_TRANSITION_NONE;
        LR_EXIT_CRITICAL();
        return 0;
    }
    s_transition_started = xTaskGetTickCount();
    s_transition = LR_TRANSITION_COLD;
    LR_EXIT_CRITICAL();
    return 0;
}

int lightring_cold_on(void)
{
    return set_cold(true);
}

int lightring_cold_off(void)
{
    return set_cold(false);
}

lightring_status_t lightring_get_status(void)
{
    if (s_output_failed) {
        return LIGHTRING_STATUS_ERROR;
    }
    return status_for_transition(s_transition);
}

lightring_mode_t lightring_get_mode(void)
{
    return s_mode;
}

lightring_speed_t lightring_get_speed(void)
{
    return s_speed;
}

int lightring_is_cold(void)
{
    return s_cold ? 1 : 0;
}
