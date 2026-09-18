#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lv_decoder.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_private.h"
#include "png.h"
#include "slide_player.h"

#ifndef BOARD_HAS_BUTTON
#define BOARD_HAS_BUTTON 0
#endif

#if !BOARD_HAS_SD || (!BOARD_HAS_TOUCH && !BOARD_HAS_BUTTON) || !BOARD_FEATURE_SLIDE_PLAYER
#error "slide_player requires SD storage and touch or button input"
#endif

static const char * TAG = "app_slide_player";
static const char * SLIDE_ASSET_DIR = "A:/sdcard";
static const uint32_t FIRST_SLIDE_NUMBER = 1U;
static const uint32_t SD_TASK_STACK_SIZE = 4096U;
static const UBaseType_t SD_TASK_PRIORITY = 4U;
static const uint32_t SERIAL_TASK_STACK_SIZE = 3072U;
static const UBaseType_t SERIAL_TASK_PRIORITY = 4U;
static const size_t SERIAL_COMMAND_MAX_LEN = 16U;
#if BOARD_HAS_BUTTON
static const uint32_t BUTTON_TASK_STACK_SIZE = 2048U;
static const UBaseType_t BUTTON_TASK_PRIORITY = 4U;
static const uint32_t BUTTON_DEBOUNCE_MS = 30U;
#endif

typedef struct {
    uint32_t request_id;
    uint32_t slide_index;
    char image_path[SLIDE_PLAYER_IMAGE_PATH_MAX_LEN];
} slide_load_request_t;

typedef enum {
    RGB565_CONVERSION_LUT,
    RGB565_CONVERSION_SHIFT,
} rgb565_conversion_t;

static esp_lv_decoder_handle_t s_decoder_handle;
static lv_image_decoder_t * s_rgb565_decoder;
static QueueHandle_t s_request_queue;
static TaskHandle_t s_reader_task_handle;
static TaskHandle_t s_serial_task_handle;
static uint32_t s_slide_count;
static rgb565_conversion_t s_rgb565_conversion = RGB565_CONVERSION_LUT;
static uint8_t s_quantize_lut[2][256];
#if BOARD_HAS_BUTTON
static TaskHandle_t s_button_task_handle;
#endif

static bool display_lock_forever(void)
{
    return bsp_display_lock(UINT32_MAX) == ESP_OK;
}

static uint32_t read_be32(const uint8_t * data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static lv_result_t rgb565_decoder_info(lv_image_decoder_t * decoder,
                                       lv_image_decoder_dsc_t * dsc,
                                       lv_image_header_t * header)
{
    (void)decoder;

    if (dsc->src_type != LV_IMAGE_SRC_FILE || strcasecmp(lv_fs_get_ext(dsc->src), "png") != 0) {
        return LV_RESULT_INVALID;
    }

    uint8_t png_header[24];
    uint32_t bytes_read = 0U;
    static const uint8_t png_signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (lv_fs_read(&dsc->file, png_header, sizeof(png_header), &bytes_read) != LV_FS_RES_OK ||
        bytes_read != sizeof(png_header) || memcmp(png_header, png_signature, sizeof(png_signature)) != 0) {
        return LV_RESULT_INVALID;
    }

    header->cf = LV_COLOR_FORMAT_RGB565A8;
    header->w = read_be32(&png_header[16]);
    header->h = read_be32(&png_header[20]);
    header->stride = header->w * 2U;
    return header->w > 0U && header->h > 0U ? LV_RESULT_OK : LV_RESULT_INVALID;
}

static lv_result_t rgb565_decoder_open(lv_image_decoder_t * decoder,
                                       lv_image_decoder_dsc_t * dsc)
{
    lv_fs_file_t file;
    uint32_t file_size = 0U;
    uint32_t bytes_read = 0U;
    uint8_t * file_data = NULL;
    uint8_t * bgra = NULL;
    lv_draw_buf_t * decoded = NULL;
    png_image image = {.version = PNG_IMAGE_VERSION};

    if (lv_fs_open(&file, dsc->src, LV_FS_MODE_RD) != LV_FS_RES_OK) {
        return LV_RESULT_INVALID;
    }
    if (lv_fs_seek(&file, 0U, LV_FS_SEEK_END) != LV_FS_RES_OK ||
        lv_fs_tell(&file, &file_size) != LV_FS_RES_OK || file_size == 0U ||
        lv_fs_seek(&file, 0U, LV_FS_SEEK_SET) != LV_FS_RES_OK) {
        goto cleanup;
    }

    file_data = malloc(file_size);
    if (file_data == NULL || lv_fs_read(&file, file_data, file_size, &bytes_read) != LV_FS_RES_OK ||
        bytes_read != file_size || !png_image_begin_read_from_memory(&image, file_data, file_size)) {
        goto cleanup;
    }

    image.format = PNG_FORMAT_BGRA;
    bgra = malloc(PNG_IMAGE_SIZE(image));
    decoded = lv_draw_buf_create(image.width, image.height, LV_COLOR_FORMAT_RGB565A8, image.width * 2U);
    if (bgra == NULL || decoded == NULL || !png_image_finish_read(&image, NULL, bgra, 0, NULL)) {
        goto cleanup;
    }

    uint16_t * rgb565 = (uint16_t *)decoded->data;
    uint8_t * alpha = (uint8_t *)decoded->data + decoded->header.stride * decoded->header.h;
    const size_t pixel_count = (size_t)image.width * image.height;
    for (size_t pixel_index = 0U; pixel_index < pixel_count; pixel_index++) {
        const uint8_t blue = bgra[pixel_index * 4U];
        const uint8_t green = bgra[pixel_index * 4U + 1U];
        const uint8_t red = bgra[pixel_index * 4U + 2U];
        const uint8_t red5 = s_rgb565_conversion == RGB565_CONVERSION_LUT
                                 ? s_quantize_lut[0][red]
                                 : red >> 3;
        const uint8_t green6 = s_rgb565_conversion == RGB565_CONVERSION_LUT
                                   ? s_quantize_lut[1][green]
                                   : green >> 2;
        const uint8_t blue5 = s_rgb565_conversion == RGB565_CONVERSION_LUT
                                  ? s_quantize_lut[0][blue]
                                  : blue >> 3;
        rgb565[pixel_index] = ((uint16_t)red5 << 11) + ((uint16_t)green6 << 5) + blue5;
        alpha[pixel_index] = bgra[pixel_index * 4U + 3U];
    }

    dsc->decoded = decoded;
    if (!dsc->args.no_cache && lv_image_cache_is_enabled()) {
        lv_image_cache_data_t search_key = {
            .src_type = dsc->src_type,
            .src = dsc->src,
            .slot.size = decoded->data_size,
        };
        dsc->cache_entry = lv_image_decoder_add_to_cache(decoder, &search_key, decoded, NULL);
        if (dsc->cache_entry == NULL) {
            dsc->decoded = NULL;
            goto cleanup;
        }
    }

    png_image_free(&image);
    free(bgra);
    free(file_data);
    lv_fs_close(&file);
    return LV_RESULT_OK;

cleanup:
    if (decoded != NULL) {
        lv_draw_buf_destroy(decoded);
    }
    png_image_free(&image);
    free(bgra);
    free(file_data);
    lv_fs_close(&file);
    return LV_RESULT_INVALID;
}

static void rgb565_decoder_close(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc)
{
    (void)decoder;

    if (dsc->cache_entry == NULL && dsc->decoded != NULL) {
        lv_draw_buf_destroy((lv_draw_buf_t *)dsc->decoded);
    }
}

static esp_err_t rgb565_decoder_init(void)
{
    for (uint32_t value = 0U; value < 256U; value++) {
        s_quantize_lut[0][value] = (uint8_t)((value * 31U + 127U) / 255U);
        s_quantize_lut[1][value] = (uint8_t)((value * 63U + 127U) / 255U);
    }

    s_rgb565_decoder = lv_image_decoder_create();
    if (s_rgb565_decoder == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_image_decoder_set_info_cb(s_rgb565_decoder, rgb565_decoder_info);
    lv_image_decoder_set_open_cb(s_rgb565_decoder, rgb565_decoder_open);
    lv_image_decoder_set_close_cb(s_rgb565_decoder, rgb565_decoder_close);
    return ESP_OK;
}

static bool set_rgb565_conversion(rgb565_conversion_t conversion)
{
    /* The legacy shifts truncate toward zero and can miss by a full RGB565 step;
       the LUT rounds to the nearest representable level and halves the maximum error. */
    s_rgb565_conversion = conversion;
    ESP_LOGI(TAG, "PNG RGB565 conversion: %s",
             conversion == RGB565_CONVERSION_LUT ? "lut" : "shift");
    return slide_player_reload();
}

static void slide_serial_task(void * arg)
{
    (void)arg;

    char command[SERIAL_COMMAND_MAX_LEN];
    while (true) {
        if (fgets(command, sizeof(command), stdin) == NULL) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20U));
            continue;
        }

        command[strcspn(command, "\r\n")] = '\0';
        bool requested = false;
        if (!display_lock_forever()) {
            ESP_LOGW(TAG, "Serial command could not lock display");
            continue;
        }

        if (strcasecmp(command, "next") == 0) {
            requested = slide_player_show_next();
        } else if (strcasecmp(command, "last") == 0) {
            requested = slide_player_show_previous();
        } else if (strcasecmp(command, "rgb565_lut") == 0) {
            requested = set_rgb565_conversion(RGB565_CONVERSION_LUT);
        } else if (strcasecmp(command, "rgb565_shift") == 0) {
            requested = set_rgb565_conversion(RGB565_CONVERSION_SHIFT);
        } else {
            char * end = NULL;
            errno = 0;
            const unsigned long slide_number = strtoul(command, &end, 10);
            if (errno == 0 && end != command && *end == '\0' && slide_number <= UINT32_MAX) {
                requested = slide_player_show_slide((uint32_t)slide_number);
            } else {
                ESP_LOGW(TAG, "Unknown serial command: %s", command);
            }
        }
        bsp_display_unlock();

        if (!requested) {
            ESP_LOGW(TAG, "Serial slide request was rejected: %s", command);
        }
    }
}

static esp_err_t slide_serial_start(void)
{
    if (xTaskCreate(slide_serial_task, "slide_serial", SERIAL_TASK_STACK_SIZE, NULL,
                    SERIAL_TASK_PRIORITY, &s_serial_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Serial controls ready: next, last, rgb565_lut, rgb565_shift, 1-%u",
             (unsigned int)s_slide_count);
    return ESP_OK;
}

#if BOARD_HAS_BUTTON
static void slide_button_task(void * arg)
{
    (void)arg;

    while (true) {
        if (gpio_get_level(BOARD_BUTTON_GPIO) != BOARD_BUTTON_ACTIVE_LEVEL) {
            vTaskDelay(pdMS_TO_TICKS(20U));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
        if (gpio_get_level(BOARD_BUTTON_GPIO) == BOARD_BUTTON_ACTIVE_LEVEL &&
            display_lock_forever()) {
            if (!slide_player_show_next()) {
                ESP_LOGW(TAG, "Button slide request was rejected");
            }
            bsp_display_unlock();
        }

        while (gpio_get_level(BOARD_BUTTON_GPIO) == BOARD_BUTTON_ACTIVE_LEVEL) {
            vTaskDelay(pdMS_TO_TICKS(20U));
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
    }
}

static esp_err_t slide_button_start(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure slide button: %s", esp_err_to_name(ret));
        return ret;
    }

    if (xTaskCreate(slide_button_task, "slide_button", BUTTON_TASK_STACK_SIZE, NULL,
                    BUTTON_TASK_PRIORITY, &s_button_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
#endif

static bool build_slide_path(uint32_t slide_index, const char * extension,
                             char * output, size_t output_size)
{
    if (extension == NULL || output == NULL || output_size == 0U || slide_index >= s_slide_count) {
        return false;
    }

    const int written = snprintf(output, output_size, "%s/%u.%s", SLIDE_ASSET_DIR,
                                 (unsigned int)(slide_index + FIRST_SLIDE_NUMBER), extension);
    return written > 0 && (size_t)written < output_size;
}

static bool parse_slide_number(const char * filename, uint32_t * slide_number)
{
    if (filename == NULL || slide_number == NULL || filename[0] < '1' || filename[0] > '9') {
        return false;
    }

    char * extension = NULL;
    errno = 0;
    const unsigned long number = strtoul(filename, &extension, 10);
    if (errno != 0 || number == 0U || number > UINT32_MAX ||
        (strcmp(extension, ".png") != 0 && strcmp(extension, ".gif") != 0)) {
        return false;
    }

    *slide_number = (uint32_t)number;
    return true;
}

static esp_err_t detect_slide_count(void)
{
    DIR * directory = opendir(BSP_SD_MOUNT_POINT);
    if (directory == NULL) {
        ESP_LOGE(TAG, "Failed to scan %s: %s", BSP_SD_MOUNT_POINT, strerror(errno));
        return ESP_FAIL;
    }

    uint32_t highest_slide_number = 0U;
    int scan_error = 0;
    while (true) {
        errno = 0;
        const struct dirent * entry = readdir(directory);
        if (entry == NULL) {
            scan_error = errno;
            break;
        }

        uint32_t slide_number = 0U;
        if (parse_slide_number(entry->d_name, &slide_number) &&
            slide_number > highest_slide_number) {
            highest_slide_number = slide_number;
        }
    }
    closedir(directory);

    if (scan_error != 0) {
        ESP_LOGE(TAG, "Failed while scanning %s: %s", BSP_SD_MOUNT_POINT,
                 strerror(scan_error));
        return ESP_FAIL;
    }
    if (highest_slide_number == 0U) {
        ESP_LOGE(TAG, "No numbered PNG or GIF slides found in %s", BSP_SD_MOUNT_POINT);
        return ESP_ERR_NOT_FOUND;
    }

    s_slide_count = highest_slide_number;
    ESP_LOGI(TAG, "Detected slides 1-%u", (unsigned int)s_slide_count);
    return ESP_OK;
}

static bool lvgl_path_to_posix_path(const char * lvgl_path, char * posix_path, size_t posix_path_size)
{
    if (lvgl_path == NULL || posix_path == NULL || posix_path_size == 0U) {
        return false;
    }

    const char * source = lvgl_path;
    if (lvgl_path[0] != '\0' && lvgl_path[1] == ':') {
        source = &lvgl_path[2];
    }

    const size_t source_len = strlen(source);
    if (source_len + 1U > posix_path_size) {
        return false;
    }

    memcpy(posix_path, source, source_len + 1U);
    return true;
}

static bool probe_slide_file(slide_player_load_result_t * result)
{
    result->error_no = 0;

    char posix_path[SLIDE_PLAYER_IMAGE_PATH_MAX_LEN];
    if (!lvgl_path_to_posix_path(result->image_path, posix_path, sizeof(posix_path))) {
        result->error_no = ENAMETOOLONG;
        return false;
    }

    FILE * file = fopen(posix_path, "rb");
    if (file == NULL) {
        result->error_no = errno;
        return false;
    }

    const int64_t start_us = esp_timer_get_time();
    uint8_t header[16];
    const size_t header_size = fread(header, 1U, sizeof(header), file);
    if (header_size == 0U && ferror(file) != 0) {
        result->error_no = errno;
        fclose(file);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        result->error_no = errno;
        fclose(file);
        return false;
    }

    const long file_size = ftell(file);
    if (file_size < 0) {
        result->error_no = errno;
        fclose(file);
        return false;
    }

    result->bytes_read = (uint32_t)file_size;
    result->elapsed_us = (uint64_t)(esp_timer_get_time() - start_us);
    fclose(file);
    return true;
}

static void slide_reader_task(void * arg)
{
    (void)arg;

    slide_load_request_t request;
    while (true) {
        if (xQueueReceive(s_request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        slide_player_load_result_t result = {
            .request_id = request.request_id,
            .slide_index = request.slide_index,
        };
        memcpy(result.image_path, request.image_path, sizeof(result.image_path));

        if (!display_lock_forever()) {
            result.error_no = EBUSY;
        } else {
            const esp_err_t wait_ret = bsp_display_wait_idle();
            if (wait_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to wait for display SPI: %s", esp_err_to_name(wait_ret));
                result.error_no = EIO;
            } else {
                result.success = probe_slide_file(&result);
                if (!result.success && result.error_no == ENOENT &&
                    build_slide_path(result.slide_index, "gif", result.image_path,
                                     sizeof(result.image_path))) {
                    result.success = probe_slide_file(&result);
                }
#if BOARD_HAS_BUTTON
                if (!result.success && result.slide_index != 0U) {
                    ESP_LOGI(TAG, "[%u] Falling back to slide 1",
                             (unsigned int)result.request_id);
                    result.slide_index = 0U;
                    if (build_slide_path(0U, "png", result.image_path,
                                         sizeof(result.image_path))) {
                        result.success = probe_slide_file(&result);
                    }
                    if (!result.success && result.error_no == ENOENT &&
                        build_slide_path(0U, "gif", result.image_path,
                                         sizeof(result.image_path))) {
                        result.success = probe_slide_file(&result);
                    }
                }
#endif
            }
            bsp_display_unlock();
        }

        if (!result.success) {
            ESP_LOGW(TAG, "[%u] SD read failed path=%s errno=%d (%s)",
                     (unsigned int)result.request_id, result.image_path,
                     result.error_no, strerror(result.error_no));
        }
        if (!slide_player_post_load_result(&result)) {
            ESP_LOGW(TAG, "[%u] Failed to post SD result", (unsigned int)result.request_id);
        }
    }
}

static bool request_slide_load(uint32_t slide_index, uint32_t request_id, void * user_ctx)
{
    (void)user_ctx;

    if (s_request_queue == NULL) {
        return false;
    }

    slide_load_request_t request = {
        .request_id = request_id,
        .slide_index = slide_index,
    };
    if (!build_slide_path(slide_index, "png", request.image_path, sizeof(request.image_path))) {
        ESP_LOGE(TAG, "[%u] Failed to build path for slide %u", (unsigned int)request_id,
                 (unsigned int)(slide_index + 1U));
        return false;
    }

    if (xQueueOverwrite(s_request_queue, &request) != pdTRUE) {
        ESP_LOGW(TAG, "[%u] Failed to queue slide %u", (unsigned int)request_id,
                 (unsigned int)(slide_index + 1U));
        return false;
    }

    ESP_LOGI(TAG, "[%u] Queued slide=%u", (unsigned int)request_id,
             (unsigned int)(slide_index + 1U));
    return true;
}

static void slide_pipeline_stop(void)
{
    if (s_reader_task_handle != NULL) {
        vTaskDelete(s_reader_task_handle);
        s_reader_task_handle = NULL;
    }
    if (s_request_queue != NULL) {
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
    }
}

static esp_err_t slide_pipeline_start(void)
{
    s_request_queue = xQueueCreate(1U, sizeof(slide_load_request_t));
    if (s_request_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create SD request queue");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(slide_reader_task, "slide_sd_reader", SD_TASK_STACK_SIZE, NULL,
                    SD_TASK_PRIORITY, &s_reader_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SD reader task");
        slide_pipeline_stop();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void slide_player_runtime_deinit(void)
{
    if (s_serial_task_handle != NULL) {
        vTaskDelete(s_serial_task_handle);
        s_serial_task_handle = NULL;
    }
#if BOARD_HAS_BUTTON
    if (s_button_task_handle != NULL) {
        vTaskDelete(s_button_task_handle);
        s_button_task_handle = NULL;
    }
#endif
    slide_pipeline_stop();

    if (s_rgb565_decoder != NULL) {
        lv_image_cache_drop(NULL);
        lv_image_decoder_delete(s_rgb565_decoder);
        s_rgb565_decoder = NULL;
    }

    if (s_decoder_handle != NULL) {
        const esp_err_t ret = esp_lv_decoder_deinit(s_decoder_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "LVGL decoder rollback failed: %s", esp_err_to_name(ret));
        }
        s_decoder_handle = NULL;
    }

    const esp_err_t ret = bsp_sdcard_unmount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD rollback failed: %s", esp_err_to_name(ret));
    }
}

static esp_err_t slide_player_runtime_init(void)
{
    esp_err_t ret = bsp_display_wait_idle();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wait for display SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = bsp_sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SD card mounted at %s", BSP_SD_MOUNT_POINT);

    ret = detect_slide_count();
    if (ret != ESP_OK) {
        bsp_sdcard_unmount();
        return ret;
    }

    ret = esp_lv_decoder_init(&s_decoder_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL decoder init failed: %s", esp_err_to_name(ret));
        const esp_err_t unmount_ret = bsp_sdcard_unmount();
        if (unmount_ret != ESP_OK) {
            ESP_LOGE(TAG, "SD rollback failed: %s", esp_err_to_name(unmount_ret));
        }
        return ret;
    }

    ret = rgb565_decoder_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGB565 decoder init failed: %s", esp_err_to_name(ret));
        slide_player_runtime_deinit();
        return ret;
    }

    ret = slide_pipeline_start();
    if (ret != ESP_OK) {
        slide_player_runtime_deinit();
    }
    return ret;
}

void app_main(void)
{
    if (bsp_display_start() == NULL) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }

    if (!display_lock_forever()) {
        ESP_LOGE(TAG, "Failed to lock display for runtime initialization");
        return;
    }

    const esp_err_t ret = slide_player_runtime_init();
    if (ret != ESP_OK) {
        bsp_display_unlock();
        return;
    }

    const slide_player_model_t model = {
        .slide_count = s_slide_count,
        .request_slide = request_slide_load,
        .user_ctx = NULL,
    };
    esp_err_t control_ret = slide_player_ui_init(&model);
    if (control_ret != ESP_OK) {
        ESP_LOGE(TAG, "UI initialization failed: %s", esp_err_to_name(control_ret));
        slide_player_runtime_deinit();
    }
#if BOARD_HAS_BUTTON
    else {
        control_ret = slide_button_start();
        if (control_ret != ESP_OK) {
            ESP_LOGE(TAG, "Button initialization failed: %s", esp_err_to_name(control_ret));
            slide_player_runtime_deinit();
        }
    }
#endif
    if (control_ret == ESP_OK) {
        control_ret = slide_serial_start();
        if (control_ret != ESP_OK) {
            ESP_LOGE(TAG, "Serial control initialization failed: %s", esp_err_to_name(control_ret));
            slide_player_runtime_deinit();
        }
    }

    bsp_display_unlock();
}
