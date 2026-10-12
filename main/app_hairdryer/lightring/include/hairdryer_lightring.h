#pragma once

#include "esp_err.h"

esp_err_t hairdryer_lightring_init(void);
void hairdryer_lightring_button_mode_click(void);
void hairdryer_lightring_button_cold_pressed(void);
void hairdryer_lightring_button_cold_released(void);
void hairdryer_lightring_button_switch_off(void);
void hairdryer_lightring_button_switch_low(void);
void hairdryer_lightring_button_switch_high(void);
