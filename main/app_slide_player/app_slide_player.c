#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

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
static const uint32_t DEFAULT_FOLDER_FPS = 20U;
static const uint32_t MAX_FOLDER_FPS = 1000U;
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
static uint32_t s_folder_fps = DEFAULT_FOLDER_FPS;
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

static void fit_image_dimensions(uint32_t source_width, uint32_t source_height,
                                 uint32_t * output_width, uint32_t * output_height)
{
    const lv_display_t * display = lv_display_get_default();
    const uint32_t max_width = display != NULL
                                   ? (uint32_t)lv_display_get_horizontal_resolution(display)
                                   : source_width;
    const uint32_t max_height = display != NULL
                                    ? (uint32_t)lv_display_get_vertical_resolution(display)
                                    : source_height;

    *output_width = source_width;
    *output_height = source_height;
    if (source_width <= max_width && source_height <= max_height) {
        return;
    }

    if ((uint64_t)source_width * max_height > (uint64_t)source_height * max_width) {
        *output_width = max_width;
        *output_height = (uint32_t)(((uint64_t)source_height * max_width) / source_width);
    } else {
        *output_height = max_height;
        *output_width = (uint32_t)(((uint64_t)source_width * max_height) / source_height);
    }
    if (*output_width == 0U) {
        *output_width = 1U;
    }
    if (*output_height == 0U) {
        *output_height = 1U;
    }
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
    const uint32_t source_width = read_be32(&png_header[16]);
    const uint32_t source_height = read_be32(&png_header[20]);
    uint32_t output_width = 0U;
    uint32_t output_height = 0U;
    fit_image_dimensions(source_width, source_height, &output_width, &output_height);
    header->w = output_width;
    header->h = output_height;
    header->stride = header->w * 2U;
    return header->w > 0U && header->h > 0U ? LV_RESULT_OK : LV_RESULT_INVALID;
}

typedef struct {
    lv_fs_file_t file;
    png_structp png;
    png_infop info;
    lv_draw_buf_t * decoded;
    uint8_t * row;
    const char * failure;
    uint32_t source_width;
    uint32_t source_height;
    bool file_open;
} png_decode_state_t;

static void png_read_from_lvgl(png_structp png, png_bytep output, png_size_t length)
{
    png_decode_state_t * state = png_get_io_ptr(png);
    uint32_t bytes_read = 0U;
    if (length > UINT32_MAX ||
        lv_fs_read(&state->file, output, (uint32_t)length, &bytes_read) != LV_FS_RES_OK ||
        bytes_read != length) {
        png_error(png, "LVGL file read failed");
    }
}

static lv_result_t rgb565_decoder_open(lv_image_decoder_t * decoder,
                                       lv_image_decoder_dsc_t * dsc)
{
    png_decode_state_t * state = calloc(1U, sizeof(*state));
    if (state == NULL) {
        return LV_RESULT_INVALID;
    }
    state->failure = "open file";
    if (lv_fs_open(&state->file, dsc->src, LV_FS_MODE_RD) != LV_FS_RES_OK) {
        goto cleanup;
    }
    state->file_open = true;

    state->failure = "initialize decoder";
    state->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (state->png == NULL) {
        goto cleanup;
    }
    state->info = png_create_info_struct(state->png);
    if (state->info == NULL || setjmp(png_jmpbuf(state->png)) != 0) {
        goto cleanup;
    }

    png_set_read_fn(state->png, state, png_read_from_lvgl);
    png_read_info(state->png, state->info);
    state->source_width = png_get_image_width(state->png, state->info);
    state->source_height = png_get_image_height(state->png, state->info);
    const int color_type = png_get_color_type(state->png, state->info);
    const int bit_depth = png_get_bit_depth(state->png, state->info);
    if (png_get_interlace_type(state->png, state->info) != PNG_INTERLACE_NONE) {
        state->failure = "interlaced PNG is unsupported";
        goto cleanup;
    }
    if (bit_depth == 16) {
        png_set_strip_16(state->png);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(state->png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(state->png);
    }
    const bool has_transparency = png_get_valid(state->png, state->info, PNG_INFO_tRNS) != 0U;
    if (has_transparency) {
        png_set_tRNS_to_alpha(state->png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(state->png);
    }
    if ((color_type & PNG_COLOR_MASK_ALPHA) == 0 && !has_transparency) {
        png_set_add_alpha(state->png, 0xFFU, PNG_FILLER_AFTER);
    }
    png_read_update_info(state->png, state->info);

    uint32_t output_width = 0U;
    uint32_t output_height = 0U;
    fit_image_dimensions(state->source_width, state->source_height,
                         &output_width, &output_height);
    state->failure = "allocate output";
    state->decoded = lv_draw_buf_create(output_width, output_height,
                                        LV_COLOR_FORMAT_RGB565A8, output_width * 2U);
    const size_t row_size = png_get_rowbytes(state->png, state->info);
    state->row = malloc(row_size);
    if (state->decoded == NULL || state->row == NULL ||
        png_get_channels(state->png, state->info) != 4U) {
        goto cleanup;
    }

    state->failure = "decode pixels";
    uint16_t * rgb565 = (uint16_t *)state->decoded->data;
    uint8_t * alpha = (uint8_t *)state->decoded->data +
                      state->decoded->header.stride * state->decoded->header.h;
    uint32_t output_y = 0U;
    uint32_t source_y_to_copy = state->source_height / (output_height * 2U);
    for (uint32_t source_y = 0U; source_y < state->source_height; source_y++) {
        png_read_row(state->png, state->row, NULL);
        if (source_y != source_y_to_copy) {
            continue;
        }

        for (uint32_t output_x = 0U; output_x < output_width; output_x++) {
            const uint32_t source_x = (uint32_t)(((uint64_t)(output_x * 2U + 1U) *
                                                  state->source_width) /
                                                 (output_width * 2U));
            const uint8_t * pixel = &state->row[source_x * 4U];
            const uint8_t red5 = s_rgb565_conversion == RGB565_CONVERSION_LUT
                                     ? s_quantize_lut[0][pixel[0]]
                                     : pixel[0] >> 3;
            const uint8_t green6 = s_rgb565_conversion == RGB565_CONVERSION_LUT
                                       ? s_quantize_lut[1][pixel[1]]
                                       : pixel[1] >> 2;
            const uint8_t blue5 = s_rgb565_conversion == RGB565_CONVERSION_LUT
                                      ? s_quantize_lut[0][pixel[2]]
                                      : pixel[2] >> 3;
            const size_t output_index = (size_t)output_y * output_width + output_x;
            rgb565[output_index] = ((uint16_t)red5 << 11) + ((uint16_t)green6 << 5) + blue5;
            alpha[output_index] = pixel[3];
        }

        output_y++;
        if (output_y < output_height) {
            source_y_to_copy = (uint32_t)(((uint64_t)(output_y * 2U + 1U) *
                                           state->source_height) /
                                          (output_height * 2U));
        }
    }
    png_read_end(state->png, NULL);

    dsc->decoded = state->decoded;
    if (!dsc->args.no_cache && lv_image_cache_is_enabled()) {
        lv_image_cache_data_t search_key = {
            .src_type = dsc->src_type,
            .src = dsc->src,
            .slot.size = state->decoded->data_size,
        };
        dsc->cache_entry = lv_image_decoder_add_to_cache(decoder, &search_key,
                                                         state->decoded, NULL);
        if (dsc->cache_entry == NULL) {
            dsc->decoded = NULL;
            goto cleanup;
        }
    }

    state->decoded = NULL;
    png_destroy_read_struct(&state->png, &state->info, NULL);
    free(state->row);
    lv_fs_close(&state->file);
    free(state);
    return LV_RESULT_OK;

cleanup:
    ESP_LOGE(TAG, "PNG decode failed (%s): %s size=%ux%u",
             state->failure, (const char *)dsc->src,
             (unsigned int)state->source_width, (unsigned int)state->source_height);
    if (state->decoded != NULL) {
        lv_draw_buf_destroy(state->decoded);
    }
    if (state->png != NULL) {
        png_destroy_read_struct(&state->png, &state->info, NULL);
    }
    free(state->row);
    if (state->file_open) {
        lv_fs_close(&state->file);
    }
    free(state);
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

static bool set_folder_fps(const char * command)
{
    if (strncasecmp(command, "set_", 4U) != 0) {
        return false;
    }

    char * suffix = NULL;
    errno = 0;
    const unsigned long fps = strtoul(command + 4U, &suffix, 10);
    if (errno != 0 || suffix == command + 4U || strcasecmp(suffix, "fps") != 0 ||
        fps == 0U || fps > MAX_FOLDER_FPS) {
        return false;
    }

    s_folder_fps = (uint32_t)fps;
    ESP_LOGI(TAG, "Folder playback rate: %u FPS", (unsigned int)s_folder_fps);
    return true;
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
        } else if (set_folder_fps(command)) {
            requested = true;
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
    ESP_LOGI(TAG, "Serial controls ready: next, last, set_<1-%u>fps, rgb565_lut, rgb565_shift, 1-%u",
             (unsigned int)MAX_FOLDER_FPS,
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

static bool build_folder_frame_path(uint32_t slide_index, uint32_t frame_number,
                                    char * output, size_t output_size)
{
    if (output == NULL || output_size == 0U || slide_index >= s_slide_count ||
        frame_number == 0U) {
        return false;
    }

    const int written = snprintf(output, output_size, "%s/%u/%u.png", SLIDE_ASSET_DIR,
                                 (unsigned int)(slide_index + FIRST_SLIDE_NUMBER),
                                 (unsigned int)frame_number);
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

static bool parse_slide_directory_number(const char * filename, uint32_t * slide_number)
{
    if (filename == NULL || slide_number == NULL || filename[0] < '1' || filename[0] > '9') {
        return false;
    }

    char * end = NULL;
    errno = 0;
    const unsigned long number = strtoul(filename, &end, 10);
    if (errno != 0 || number == 0U || number > UINT32_MAX || *end != '\0') {
        return false;
    }

    char path[SLIDE_PLAYER_IMAGE_PATH_MAX_LEN];
    const int written = snprintf(path, sizeof(path), "%s/%s", BSP_SD_MOUNT_POINT, filename);
    struct stat info;
    if (written <= 0 || (size_t)written >= sizeof(path) || stat(path, &info) != 0 ||
        !S_ISDIR(info.st_mode)) {
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
                if ((parse_slide_number(entry->d_name, &slide_number) ||
                         parse_slide_directory_number(entry->d_name, &slide_number)) &&
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
        ESP_LOGE(TAG, "No numbered PNG, GIF, or frame directories found in %s",
                 BSP_SD_MOUNT_POINT);
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
    uint32_t next_frame_number = 0U;
    TickType_t wait_ticks = portMAX_DELAY;
    while (true) {
        const bool new_request = xQueueReceive(s_request_queue, &request, wait_ticks) == pdTRUE;
        if (new_request) {
            next_frame_number = 0U;
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
                if (next_frame_number != 0U) {
                    if (build_folder_frame_path(result.slide_index, next_frame_number,
                                                result.image_path, sizeof(result.image_path))) {
                        result.success = probe_slide_file(&result);
                    }
                    if (!result.success && result.error_no == ENOENT &&
                        next_frame_number != 1U &&
                        build_folder_frame_path(result.slide_index, 1U, result.image_path,
                                                sizeof(result.image_path))) {
                        result.success = probe_slide_file(&result);
                        next_frame_number = 1U;
                    }
                } else {
                    result.success = probe_slide_file(&result);
                    if (!result.success && result.error_no == ENOENT &&
                        build_slide_path(result.slide_index, "gif", result.image_path,
                                         sizeof(result.image_path))) {
                        result.success = probe_slide_file(&result);
                    }
                    if (!result.success && result.error_no == ENOENT &&
                        build_folder_frame_path(result.slide_index, 1U, result.image_path,
                                                sizeof(result.image_path))) {
                        result.success = probe_slide_file(&result);
                        if (result.success) {
                            next_frame_number = 1U;
                            ESP_LOGI(TAG, "[%u] Playing folder slide=%u at %u FPS path=%s bytes=%u",
                                     (unsigned int)result.request_id,
                                     (unsigned int)(result.slide_index + FIRST_SLIDE_NUMBER),
                                     (unsigned int)s_folder_fps, result.image_path,
                                     (unsigned int)result.bytes_read);
                        }
                    }
                }
#if BOARD_HAS_BUTTON
                if (!result.success && next_frame_number == 0U && result.slide_index != 0U) {
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

            if (result.success && next_frame_number != 0U) {
                result.animation_frame = true;
                next_frame_number++;
                wait_ticks = pdMS_TO_TICKS((1000U + s_folder_fps - 1U) / s_folder_fps);
            } else {
                next_frame_number = 0U;
                wait_ticks = portMAX_DELAY;
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
