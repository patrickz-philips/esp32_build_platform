#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "lightring.h"
#include "lightring_effects.h"
#include "lightring_palette.h"
#include "ws2812.h"

#define LR_DEFAULT_STACK_BYTES       3072U
#define LR_DEFAULT_PRIORITY          5U
#define LR_ACTIVATE_MS               350U
#define LR_DEACTIVATE_MS             350U
#define LR_MODE_COLLAPSE_MS          300U
#define LR_MODE_DECISION_MS          750U
#define LR_EDGE_FADE_MS              200U
#define LR_EDGE_BLEND_MS             200U
#define LR_MODE_EXPAND_MS            400U
#define LR_MODE_WAIT_MS              (LR_MODE_DECISION_MS - LR_MODE_COLLAPSE_MS)

static const char *TAG = "lightring";

typedef enum {
    LR_COMMAND_ACTIVATE = 0,
    LR_COMMAND_DEACTIVATE,
    LR_COMMAND_BEGIN_MODE_CHANGE,
    LR_COMMAND_CANCEL_MODE_CHANGE,
    LR_COMMAND_COMMIT_MODE_CHANGE,
} lr_command_type_t;

typedef struct {
    lr_command_type_t          type;
    lightring_mode_t           mode;
    const lightring_palette_t *primary;
    const lightring_palette_t *secondary;
    uint16_t                   elapsed_ms;
} lr_command_t;

typedef struct {
    lightring_phase_t phase;
    uint16_t          duration_ms;
} lr_step_t;

static TaskHandle_t                s_task;
static volatile uint32_t           s_generation;
static lr_command_t                s_command;
static volatile lightring_status_t s_status = LIGHTRING_STATUS_OFF;
static lightring_mode_t             s_active_mode = LIGHTRING_MODE_REGULAR;
static lightring_mode_t             s_target_mode = LIGHTRING_MODE_REGULAR;
static TickType_t                   s_mode_change_started;
static portMUX_TYPE                 s_lock = portMUX_INITIALIZER_UNLOCKED;

static const lightring_palette_t *palette_for_mode(lightring_mode_t mode)
{
    switch (mode) {
    case LIGHTRING_MODE_INTENSE:
        return &LR_PAL_INTENSE;
    case LIGHTRING_MODE_SENSITIVE:
        return &LR_PAL_SENSITIVE;
    case LIGHTRING_MODE_REGULAR:
        return &LR_PAL_REGULAR;
    default:
        return NULL;
    }
}

static uint32_t ticks_to_ms(TickType_t ticks)
{
    return (uint32_t) (((uint64_t) ticks * 1000U) / configTICK_RATE_HZ);
}

static bool generation_is_current(uint32_t generation)
{
    return s_generation == generation;
}

static bool set_status(uint32_t generation, lightring_status_t status)
{
    bool current;

    taskENTER_CRITICAL(&s_lock);
    current = generation_is_current(generation);
    if (current) {
        s_status = status;
    }
    taskEXIT_CRITICAL(&s_lock);
    return current;
}

static bool run_step_from(const lr_step_t *step, uint16_t elapsed_offset_ms,
                          uint32_t generation, const lr_command_t *command)
{
    TickType_t start = xTaskGetTickCount();

    for (;;) {
        if (!generation_is_current(generation)) {
            return false;
        }

        uint32_t elapsed = elapsed_offset_ms + ticks_to_ms(xTaskGetTickCount() - start);
        bool last = elapsed >= step->duration_ms;
        if (last) {
            elapsed = step->duration_ms;
        }

        lightring_frame_t frame = {
            .phase = step->phase,
            .elapsed_ms = elapsed,
            .total_ms = step->duration_ms,
            .primary = command->primary,
            .secondary = command->secondary,
        };
        (void) lightring_render_phase(&frame);
        if (ws2812_send(lightring_framebuffer, LIGHTRING_LED_COUNT) != 0) {
            ESP_LOGE(TAG, "Effect output failed");
            (void)set_status(generation, LIGHTRING_STATUS_OFF);
            return false;
        }

        if (last) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(LIGHTRING_FRAME_MS));
    }
}

static bool run_step(const lr_step_t *step, uint32_t generation,
                     const lr_command_t *command)
{
    return run_step_from(step, 0U, generation, command);
}

static void complete_on(uint32_t generation, lightring_mode_t mode)
{
    taskENTER_CRITICAL(&s_lock);
    if (generation_is_current(generation)) {
        s_active_mode = mode;
        s_status = LIGHTRING_STATUS_ON;
    }
    taskEXIT_CRITICAL(&s_lock);
}

static void complete_off(uint32_t generation)
{
    taskENTER_CRITICAL(&s_lock);
    if (generation_is_current(generation)) {
        s_status = LIGHTRING_STATUS_OFF;
    }
    taskEXIT_CRITICAL(&s_lock);
}

static void run_command(const lr_command_t *command)
{
    uint32_t generation = s_generation;
    lr_step_t step;

    switch (command->type) {
    case LR_COMMAND_ACTIVATE:
        ESP_LOGI(TAG, "Effect start: activate, mode=%s", command->primary->name);
        step = (lr_step_t) { LR_PH_EXPAND, LR_ACTIVATE_MS };
        if (run_step(&step, generation, command)) {
            complete_on(generation, command->mode);
            ESP_LOGI(TAG, "Effect complete: ring on, mode=%s", command->primary->name);
        }
        break;

    case LR_COMMAND_DEACTIVATE:
        ESP_LOGI(TAG, "Effect start: deactivate, mode=%s", command->primary->name);
        step = (lr_step_t) { LR_PH_COLLAPSE_OFF, LR_DEACTIVATE_MS };
        if (run_step(&step, generation, command)) {
            complete_off(generation);
            ESP_LOGI(TAG, "Effect complete: ring off");
        }
        break;

    case LR_COMMAND_BEGIN_MODE_CHANGE:
        ESP_LOGI(TAG, "Effect start: mode preview, %s -> %s",
                 command->primary->name, command->secondary->name);
        step = (lr_step_t) { LR_PH_COLLAPSE_TO_EDGE, LR_MODE_COLLAPSE_MS };
        if (!run_step(&step, generation, command) ||
            !set_status(generation, LIGHTRING_STATUS_MODE_WAITING)) {
            break;
        }
        step = (lr_step_t) { LR_PH_HOLD_EDGE, LR_MODE_WAIT_MS };
        if (!run_step(&step, generation, command) ||
            !set_status(generation, LIGHTRING_STATUS_MODE_PREVIEW)) {
            break;
        }
        step = (lr_step_t) { LR_PH_EDGE_BLEND, LR_EDGE_BLEND_MS };
        if (run_step(&step, generation, command)) {
            ESP_LOGI(TAG, "Effect complete: mode preview, target=%s", command->secondary->name);
        }
        break;

    case LR_COMMAND_CANCEL_MODE_CHANGE:
        ESP_LOGI(TAG, "Effect start: cancel mode change");
        if (command->elapsed_ms < LR_MODE_COLLAPSE_MS) {
            const uint16_t collapse_offset_ms = (uint16_t)(
                ((uint32_t)command->elapsed_ms * (LIGHTRING_PATH_LENGTH - 1U) *
                 LR_DEACTIVATE_MS) /
                (LR_MODE_COLLAPSE_MS * LIGHTRING_PATH_LENGTH));
            step = (lr_step_t) { LR_PH_COLLAPSE_OFF, LR_DEACTIVATE_MS };
            if (!run_step_from(&step, collapse_offset_ms, generation, command)) {
                break;
            }
        } else {
            step = (lr_step_t) { LR_PH_EDGE_FADE_OUT, LR_EDGE_FADE_MS };
            if (!run_step(&step, generation, command)) {
                break;
            }
        }
        if (generation_is_current(generation)) {
            complete_off(generation);
            ESP_LOGI(TAG, "Effect complete: ring off");
        }
        break;

    case LR_COMMAND_COMMIT_MODE_CHANGE:
        ESP_LOGI(TAG, "Effect start: apply mode, mode=%s", command->primary->name);
        step = (lr_step_t) { LR_PH_EXPAND, LR_MODE_EXPAND_MS };
        if (run_step(&step, generation, command)) {
            complete_on(generation, command->mode);
            ESP_LOGI(TAG, "Effect complete: ring on, mode=%s", command->primary->name);
        }
        break;
    }
}

static void render_task(void *argument)
{
    (void) argument;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        lr_command_t command;
        taskENTER_CRITICAL(&s_lock);
        command = s_command;
        taskEXIT_CRITICAL(&s_lock);
        run_command(&command);
    }
}

static void queue_locked(const lr_command_t *command, lightring_status_t status)
{
    s_command = *command;
    s_status = status;
    ++s_generation;
}

int lightring_init(const lightring_config_t *config)
{
    if (config == NULL || s_task != NULL) {
        return -1;
    }
    if (ws2812_init(&config->ws2812) != 0) {
        return -1;
    }
    lightring_clear();
    if (ws2812_send(lightring_framebuffer, LIGHTRING_LED_COUNT) != 0) {
        ws2812_deinit();
        return -1;
    }

    uint32_t stack = config->task_stack_bytes ? config->task_stack_bytes
                                              : LR_DEFAULT_STACK_BYTES;
    UBaseType_t priority = config->task_priority ? (UBaseType_t) config->task_priority
                                                 : (UBaseType_t) LR_DEFAULT_PRIORITY;
    if (xTaskCreate(render_task, "lightring", stack, NULL, priority, &s_task) != pdPASS) {
        s_task = NULL;
        ws2812_deinit();
        return -1;
    }
    return 0;
}

int lightring_activate(lightring_mode_t mode)
{
    const lightring_palette_t *palette = palette_for_mode(mode);
    if (palette == NULL) {
        return -1;
    }

    lr_command_t command = { LR_COMMAND_ACTIVATE, mode, palette, NULL, 0U };
    taskENTER_CRITICAL(&s_lock);
    if (s_task == NULL || s_status != LIGHTRING_STATUS_OFF) {
        taskEXIT_CRITICAL(&s_lock);
        return -1;
    }
    queue_locked(&command, LIGHTRING_STATUS_ACTIVATING);
    taskEXIT_CRITICAL(&s_lock);
    xTaskNotifyGive(s_task);
    return 0;
}

int lightring_deactivate(void)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_task == NULL || s_status != LIGHTRING_STATUS_ON) {
        taskEXIT_CRITICAL(&s_lock);
        return -1;
    }
    lr_command_t command = {
        LR_COMMAND_DEACTIVATE,
        s_active_mode,
        palette_for_mode(s_active_mode),
        NULL,
        0U,
    };
    queue_locked(&command, LIGHTRING_STATUS_DEACTIVATING);
    taskEXIT_CRITICAL(&s_lock);
    xTaskNotifyGive(s_task);
    return 0;
}

int lightring_begin_mode_change(lightring_mode_t next_mode)
{
    const lightring_palette_t *next = palette_for_mode(next_mode);
    if (next == NULL) {
        return -1;
    }

    TickType_t now = xTaskGetTickCount();
    taskENTER_CRITICAL(&s_lock);
    if (s_task == NULL || s_status != LIGHTRING_STATUS_ON || next_mode == s_active_mode) {
        taskEXIT_CRITICAL(&s_lock);
        return -1;
    }
    lr_command_t command = {
        LR_COMMAND_BEGIN_MODE_CHANGE,
        next_mode,
        palette_for_mode(s_active_mode),
        next,
        0U,
    };
    s_target_mode = next_mode;
    s_mode_change_started = now;
    queue_locked(&command, LIGHTRING_STATUS_MODE_COLLAPSING);
    taskEXIT_CRITICAL(&s_lock);
    xTaskNotifyGive(s_task);
    return 0;
}

int lightring_cancel_mode_change(void)
{
    TickType_t now = xTaskGetTickCount();
    taskENTER_CRITICAL(&s_lock);
    uint32_t elapsed = ticks_to_ms(now - s_mode_change_started);
    bool changing = s_status == LIGHTRING_STATUS_MODE_COLLAPSING ||
                    s_status == LIGHTRING_STATUS_MODE_WAITING;
    if (s_task == NULL || !changing || elapsed >= LR_MODE_DECISION_MS) {
        taskEXIT_CRITICAL(&s_lock);
        return -1;
    }
    lr_command_t command = {
        LR_COMMAND_CANCEL_MODE_CHANGE,
        s_active_mode,
        palette_for_mode(s_active_mode),
        NULL,
        (uint16_t)elapsed,
    };
    queue_locked(&command, LIGHTRING_STATUS_DEACTIVATING);
    taskEXIT_CRITICAL(&s_lock);
    xTaskNotifyGive(s_task);
    return 0;
}

int lightring_commit_mode_change(void)
{
    TickType_t now = xTaskGetTickCount();
    taskENTER_CRITICAL(&s_lock);
    uint32_t elapsed = ticks_to_ms(now - s_mode_change_started);
    bool changing = s_status == LIGHTRING_STATUS_MODE_COLLAPSING ||
                    s_status == LIGHTRING_STATUS_MODE_WAITING ||
                    s_status == LIGHTRING_STATUS_MODE_PREVIEW;
    if (s_task == NULL || !changing || elapsed < LR_MODE_DECISION_MS) {
        taskEXIT_CRITICAL(&s_lock);
        return -1;
    }
    lr_command_t command = {
        LR_COMMAND_COMMIT_MODE_CHANGE,
        s_target_mode,
        palette_for_mode(s_target_mode),
        NULL,
        0U,
    };
    queue_locked(&command, LIGHTRING_STATUS_MODE_APPLYING);
    taskEXIT_CRITICAL(&s_lock);
    xTaskNotifyGive(s_task);
    return 0;
}

lightring_status_t lightring_get_status(void)
{
    return s_status;
}

lightring_mode_t lightring_get_mode(void)
{
    return s_active_mode;
}
