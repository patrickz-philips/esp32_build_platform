#pragma once

#include "esp_err.h"

#include "bsp/display.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_SD_MOUNT_POINT "/sdcard"

esp_err_t bsp_sdcard_mount(void);
esp_err_t bsp_sdcard_unmount(void);

#ifdef __cplusplus
}
#endif