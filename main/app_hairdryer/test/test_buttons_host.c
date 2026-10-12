#include <assert.h>

#include "hairdryer_buttons.h"

static size_t poll(hairdryer_buttons_t *buttons,
                   hairdryer_button_levels_t levels,
                   uint32_t now_ms,
                   hairdryer_button_event_t events[HAIRDRYER_BUTTON_EVENT_COUNT])
{
    return hairdryer_buttons_poll(buttons, levels, now_ms, events);
}

int main(void)
{
    hairdryer_button_levels_t levels = {
        .mode = 1,
        .cold = 1,
        .switch_off = 1,
        .switch_high = 1,
    };
    hairdryer_buttons_t buttons;
    hairdryer_button_event_t events[HAIRDRYER_BUTTON_EVENT_COUNT];
    hairdryer_buttons_init(&buttons, levels, 0);

    levels.mode = 0;
    assert(poll(&buttons, levels, 10, events) == 0);
    assert(poll(&buttons, levels, 40, events) == 0);
    levels.mode = 1;
    assert(poll(&buttons, levels, 50, events) == 0);
    assert(poll(&buttons, levels, 80, events) == 1);
    assert(events[0].source == HAIRDRYER_BUTTON_SOURCE_MODE);

    levels.cold = 0;
    assert(poll(&buttons, levels, 90, events) == 0);
    assert(poll(&buttons, levels, 120, events) == 1);
    assert(events[0].source == HAIRDRYER_BUTTON_SOURCE_COLD);
    assert(events[0].value.cold == HAIRDRYER_BUTTON_COLD_EVENT_PRESSED);
    levels.cold = 1;
    assert(poll(&buttons, levels, 130, events) == 0);
    assert(poll(&buttons, levels, 160, events) == 1);
    assert(events[0].value.cold == HAIRDRYER_BUTTON_COLD_EVENT_RELEASED);

    levels.switch_off = 0;
    levels.switch_high = 0;
    assert(poll(&buttons, levels, 170, events) == 0);
    assert(poll(&buttons, levels, 210, events) == 0);
    levels.switch_off = 1;
    assert(poll(&buttons, levels, 220, events) == 0);
    assert(poll(&buttons, levels, 250, events) == 1);
    assert(events[0].source == HAIRDRYER_BUTTON_SOURCE_SWITCH);
    assert(events[0].value.switch_state == HAIRDRYER_BUTTON_SWITCH_HIGH);

    levels.switch_off = 0;
    levels.switch_high = 1;
    assert(poll(&buttons, levels, 260, events) == 0);
    levels.switch_high = 0;
    assert(poll(&buttons, levels, 280, events) == 0);
    levels.switch_high = 1;
    assert(poll(&buttons, levels, 300, events) == 0);
    assert(poll(&buttons, levels, 329, events) == 0);
    assert(poll(&buttons, levels, 330, events) == 1);
    assert(events[0].value.switch_state == HAIRDRYER_BUTTON_SWITCH_OFF);

    return 0;
}
