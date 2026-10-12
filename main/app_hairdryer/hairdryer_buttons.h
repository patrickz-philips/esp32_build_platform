#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HAIRDRYER_BUTTON_MODE_EVENT_CLICK = 0,
} hairdryer_button_mode_event_t;

typedef enum {
    HAIRDRYER_BUTTON_COLD_EVENT_PRESSED = 0,
    HAIRDRYER_BUTTON_COLD_EVENT_RELEASED,
} hairdryer_button_cold_event_t;

typedef enum {
    HAIRDRYER_BUTTON_SWITCH_OFF = 0,
    HAIRDRYER_BUTTON_SWITCH_LOW,
    HAIRDRYER_BUTTON_SWITCH_HIGH,
} hairdryer_button_switch_state_t;

typedef enum {
    HAIRDRYER_BUTTON_SOURCE_MODE = 0,
    HAIRDRYER_BUTTON_SOURCE_COLD,
    HAIRDRYER_BUTTON_SOURCE_SWITCH,
} hairdryer_button_source_t;

typedef struct {
    hairdryer_button_source_t source;
    union {
        hairdryer_button_mode_event_t mode;
        hairdryer_button_cold_event_t cold;
        hairdryer_button_switch_state_t switch_state;
    } value;
} hairdryer_button_event_t;

enum {
    HAIRDRYER_BUTTON_EVENT_COUNT = 3,
};

typedef struct {
    uint8_t mode;
    uint8_t cold;
    uint8_t switch_off;
    uint8_t switch_high;
} hairdryer_button_levels_t;

typedef struct {
    bool sampled_pressed;
    bool stable_pressed;
    uint32_t last_change_ms;
} hairdryer_debounced_button_t;

typedef struct {
    hairdryer_debounced_button_t mode;
    hairdryer_debounced_button_t cold;
    bool switch_candidate_valid;
    bool switch_stable_valid;
    hairdryer_button_switch_state_t switch_candidate;
    hairdryer_button_switch_state_t switch_stable;
    uint32_t switch_last_change_ms;
} hairdryer_buttons_t;

void hairdryer_buttons_init(hairdryer_buttons_t *buttons,
                            hairdryer_button_levels_t levels,
                            uint32_t now_ms);
size_t hairdryer_buttons_poll(hairdryer_buttons_t *buttons,
                              hairdryer_button_levels_t levels,
                              uint32_t now_ms,
                              hairdryer_button_event_t events[HAIRDRYER_BUTTON_EVENT_COUNT]);
