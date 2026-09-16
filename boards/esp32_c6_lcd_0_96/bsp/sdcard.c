#include "bsp/esp-bsp.h"

#include "board_config.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static sdmmc_card_t * s_card;

esp_err_t bsp_sdcard_mount(void)
{
    if (s_card != NULL) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = SPI2_HOST;
    slot_config.gpio_cs = BOARD_SD_CS_GPIO;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    return esp_vfs_fat_sdspi_mount(BSP_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (s_card == NULL) {
        return ESP_OK;
    }

    const esp_err_t ret = esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, s_card);
    if (ret == ESP_OK) {
        s_card = NULL;
    }
    return ret;
}