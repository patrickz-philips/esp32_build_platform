#include "hairdryer_buttons.h"

static const uint32_t BUTTON_DEBOUNCE_MS = 30U;

typedef enum {
    BUTTON_EDGE_NONE = 0,
    BUTTON_EDGE_PRESSED,
    BUTTON_EDGE_RELEASED,
} button_edge_t;

static button_edge_t update_button(hairdryer_debounced_button_t *button,
                                   bool pressed,
                                   uint32_t now_ms)
{
    if (pressed != button->sampled_pressed) {
        button->sampled_pressed = pressed;
        button->last_change_ms = now_ms;
    }
    if (pressed == button->stable_pressed ||
        (now_ms - button->last_change_ms) < BUTTON_DEBOUNCE_MS) {
        return BUTTON_EDGE_NONE;
    }

    button->stable_pressed = pressed;
    return pressed ? BUTTON_EDGE_PRESSED : BUTTON_EDGE_RELEASED;
}

static bool decode_switch(hairdryer_button_levels_t levels,
                          hairdryer_button_switch_state_t *state)
{
    if (levels.switch_off == 0U && levels.switch_high == 1U) {
        *state = HAIRDRYER_BUTTON_SWITCH_OFF;
    } else if (levels.switch_off == 1U && levels.switch_high == 1U) {
        *state = HAIRDRYER_BUTTON_SWITCH_LOW;
    } else if (levels.switch_off == 1U && levels.switch_high == 0U) {
        *state = HAIRDRYER_BUTTON_SWITCH_HIGH;
    } else {
        return false;
    }
    return true;
}

void hairdryer_buttons_init(hairdryer_buttons_t *buttons,
                            hairdryer_button_levels_t levels,
                            uint32_t now_ms)
{
    *buttons = (hairdryer_buttons_t) {
        .mode = {
            .sampled_pressed = levels.mode == 0U,
            .stable_pressed = levels.mode == 0U,
            .last_change_ms = now_ms,
        },
        .cold = {
            .sampled_pressed = levels.cold == 0U,
            .stable_pressed = levels.cold == 0U,
            .last_change_ms = now_ms,
        },
        .switch_last_change_ms = now_ms,
    };

    buttons->switch_candidate_valid = decode_switch(levels, &buttons->switch_candidate);
    buttons->switch_stable_valid = buttons->switch_candidate_valid;
    buttons->switch_stable = buttons->switch_candidate;
}

size_t hairdryer_buttons_poll(hairdryer_buttons_t *buttons,
                              hairdryer_button_levels_t levels,
                              uint32_t now_ms,
                              hairdryer_button_event_t events[HAIRDRYER_BUTTON_EVENT_COUNT])
{
    size_t event_count = 0;

    if (update_button(&buttons->mode, levels.mode == 0U, now_ms) ==
        BUTTON_EDGE_RELEASED) {
        events[event_count++] = (hairdryer_button_event_t) {
            .source = HAIRDRYER_BUTTON_SOURCE_MODE,
            .value.mode = HAIRDRYER_BUTTON_MODE_EVENT_CLICK,
        };
    }

    const button_edge_t cold_edge =
        update_button(&buttons->cold, levels.cold == 0U, now_ms);
    if (cold_edge != BUTTON_EDGE_NONE) {
        events[event_count++] = (hairdryer_button_event_t) {
            .source = HAIRDRYER_BUTTON_SOURCE_COLD,
            .value.cold = cold_edge == BUTTON_EDGE_PRESSED
                              ? HAIRDRYER_BUTTON_COLD_EVENT_PRESSED
                              : HAIRDRYER_BUTTON_COLD_EVENT_RELEASED,
        };
    }

    hairdryer_button_switch_state_t switch_state;
    if (!decode_switch(levels, &switch_state)) {
        buttons->switch_candidate_valid = false;
        return event_count;
    }
    if (!buttons->switch_candidate_valid || switch_state != buttons->switch_candidate) {
        buttons->switch_candidate = switch_state;
        buttons->switch_candidate_valid = true;
        buttons->switch_last_change_ms = now_ms;
        return event_count;
    }
    if ((now_ms - buttons->switch_last_change_ms) < BUTTON_DEBOUNCE_MS) {
        return event_count;
    }
    if (!buttons->switch_stable_valid) {
        buttons->switch_stable = switch_state;
        buttons->switch_stable_valid = true;
    } else if (switch_state != buttons->switch_stable) {
        buttons->switch_stable = switch_state;
        events[event_count++] = (hairdryer_button_event_t) {
            .source = HAIRDRYER_BUTTON_SOURCE_SWITCH,
            .value.switch_state = switch_state,
        };
    }

    return event_count;
}
