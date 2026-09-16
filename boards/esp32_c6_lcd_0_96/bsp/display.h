#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_LCD_H_RES 160
#define BSP_LCD_V_RES 80

lv_display_t * bsp_display_start(void);
esp_err_t bsp_display_brightness_set(int brightness_percent);
esp_err_t bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);

#ifdef __cplusplus
}
#endif