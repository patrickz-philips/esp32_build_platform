#include "bsp/display.h"

#include "board_config.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7735.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char * TAG = "bsp_display";

#define LCD_HOST SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_DRAW_BUF_LINES 20
#define LCD_BITS_PER_PIXEL 16
#define LCD_REFRESH_PERIOD_MS 50

#define LEDC_BL_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_BL_TIMER      LEDC_TIMER_0
#define LEDC_BL_CHANNEL    LEDC_CHANNEL_0
#define LEDC_BL_DUTY_RES   LEDC_TIMER_10_BIT
#define LEDC_BL_FREQ_HZ    5000
#define LEDC_BL_MAX_DUTY   ((1U << 10) - 1U)

static lv_display_t * s_display;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;

static const st7735_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {ST7735_SLPOUT, NULL, 0, 120},
    {ST7735_MADCTL, (uint8_t[]){0x08}, 1, 0},
    {ST7735_FRMCTR1, (uint8_t[]){0x01, 0x2C, 0x2D}, 3, 0},
    {ST7735_FRMCTR2, (uint8_t[]){0x01, 0x2C, 0x2D}, 3, 0},
    {ST7735_FRMCTR3, (uint8_t[]){0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6, 0},
    {ST7735_INVCTR, (uint8_t[]){0x07}, 1, 0},
    {ST7735_INVON, NULL, 0, 0},
    {ST7735_PWCTR1, (uint8_t[]){0xA2, 0x02, 0x84}, 3, 0},
    {ST7735_PWCTR2, (uint8_t[]){0xC5}, 1, 0},
    {ST7735_PWCTR3, (uint8_t[]){0x0A, 0x00}, 2, 0},
    {ST7735_PWCTR4, (uint8_t[]){0x8A, 0x2A}, 2, 0},
    {ST7735_PWCTR5, (uint8_t[]){0x8A, 0xEE}, 2, 0},
    {ST7735_VMCTR1, (uint8_t[]){0x0E}, 1, 0},
    {ST7735_GMCTRP1, (uint8_t[]){0x0F, 0x1A, 0x0F, 0x18, 0x2F, 0x28, 0x20, 0x22,
                                 0x1F, 0x1B, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10}, 16, 0},
    {ST7735_GMCTRN1, (uint8_t[]){0x0F, 0x1B, 0x0F, 0x17, 0x33, 0x2C, 0x29, 0x2E,
                                 0x30, 0x30, 0x39, 0x3F, 0x00, 0x07, 0x03, 0x10}, 16, 0},
    {0xF0, (uint8_t[]){0x01}, 1, 0},
    {0xF6, (uint8_t[]){0x00}, 1, 0},
    {ST7735_COLMOD, (uint8_t[]){0x05}, 1, 0},
    {ST7735_DISPON, NULL, 0, 0},
};

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_BL_MODE,
        .duty_resolution = LEDC_BL_DUTY_RES,
        .timer_num = LEDC_BL_TIMER,
        .freq_hz = LEDC_BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "Failed to configure backlight timer");

    const ledc_channel_config_t channel_config = {
        .gpio_num = BOARD_LCD_BL_GPIO,
        .speed_mode = LEDC_BL_MODE,
        .channel = LEDC_BL_CHANNEL,
        .timer_sel = LEDC_BL_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&channel_config);
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent < 0) {
        brightness_percent = 0;
    } else if (brightness_percent > 100) {
        brightness_percent = 100;
    }

    const uint32_t duty = LEDC_BL_MAX_DUTY * (uint32_t)brightness_percent / 100U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_BL_MODE, LEDC_BL_CHANNEL, duty), TAG,
                        "Failed to set backlight duty");
    return ledc_update_duty(LEDC_BL_MODE, LEDC_BL_CHANNEL);
}

static esp_err_t panel_init(void)
{
    const spi_bus_config_t bus_config = {
        .mosi_io_num = BOARD_LCD_MOSI_GPIO,
        .miso_io_num = BOARD_LCD_MISO_GPIO,
        .sclk_io_num = BOARD_LCD_SCLK_GPIO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = BOARD_LCD_H_RES * LCD_DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG,
                        "Failed to initialize shared SPI bus");

    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BOARD_LCD_DC_GPIO,
        .cs_gpio_num = BOARD_LCD_CS_GPIO,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_panel_io),
        TAG, "Failed to create panel IO");

    st7735_vendor_config_t vendor_config = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7735(s_panel_io, &panel_config, &s_panel), TAG,
                        "Failed to create ST7735 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "Failed to reset panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "Failed to initialize panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 26, 1), TAG, "Failed to set panel gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "Failed to turn display on");
    return ESP_OK;
}

lv_display_t * bsp_display_start(void)
{
    if (s_display != NULL) {
        return s_display;
    }
    if (backlight_init() != ESP_OK || panel_init() != ESP_OK) {
        ESP_LOGE(TAG, "Display hardware initialization failed");
        return NULL;
    }

    const lvgl_port_cfg_t port_config = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&port_config) != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port initialization failed");
        return NULL;
    }

    const lvgl_port_display_cfg_t display_config = {
        .io_handle = s_panel_io,
        .panel_handle = s_panel,
        .buffer_size = BOARD_LCD_H_RES * LCD_DRAW_BUF_LINES,
        .double_buffer = false,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    s_display = lvgl_port_add_disp(&display_config);
    if (s_display == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return NULL;
    }
    lv_timer_set_period(lv_display_get_refr_timer(s_display), LCD_REFRESH_PERIOD_MS);

    if (bsp_display_brightness_set(100) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable backlight");
    }
    ESP_LOGI(TAG, "Display started: %dx%d portrait ST7735, refresh=20 FPS",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return s_display;
}

esp_err_t bsp_display_wait_idle(void)
{
    if (s_panel_io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_io_tx_param(s_panel_io, -1, NULL, 0);
}

esp_err_t bsp_display_lock(uint32_t timeout_ms)
{
    const uint32_t port_timeout_ms = timeout_ms == UINT32_MAX ? 0U : timeout_ms;
    return lvgl_port_lock(port_timeout_ms) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}