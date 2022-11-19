/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <sys/cdefs.h>
#include "sdkconfig.h"
#if CONFIG_LCD_ENABLE_DEBUG_LOG
// The local log level must be defined before including esp_log.h
// Set the maximum log level for this source file
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "lcd_panel.nt35510c";

static esp_err_t panel_nt35510c_del(esp_lcd_panel_t *panel);
static esp_err_t panel_nt35510c_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_nt35510c_init(esp_lcd_panel_t *panel);
static esp_err_t panel_nt35510c_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_nt35510c_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_nt35510c_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_nt35510c_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_nt35510c_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_nt35510c_disp_on_off(esp_lcd_panel_t *panel, bool off);

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    unsigned int bits_per_pixel;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_cal; // save surrent value of LCD_CMD_COLMOD register
} nt35510c_panel_t;

esp_err_t esp_lcd_new_panel_nt35510c(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
#if CONFIG_LCD_ENABLE_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    esp_err_t ret = ESP_OK;
    nt35510c_panel_t *nt35510c = NULL;
    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    nt35510c = calloc(1, sizeof(nt35510c_panel_t));
    ESP_GOTO_ON_FALSE(nt35510c, ESP_ERR_NO_MEM, err, TAG, "no mem for nt35510c panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->color_space) {
    case ESP_LCD_COLOR_SPACE_RGB:
        nt35510c->madctl_val = 0;
        break;
    case ESP_LCD_COLOR_SPACE_BGR:
        nt35510c->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color space");
        break;
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 16:
        nt35510c->colmod_cal = 0x55;
        break;
    case 18:
        nt35510c->colmod_cal = 0x66;
        break;
    case 24:
        nt35510c->colmod_cal = 0x77;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    nt35510c->io = io;
    nt35510c->bits_per_pixel = panel_dev_config->bits_per_pixel;
    nt35510c->reset_gpio_num = panel_dev_config->reset_gpio_num;
    nt35510c->reset_level = panel_dev_config->flags.reset_active_high;
    nt35510c->base.del = panel_nt35510c_del;
    nt35510c->base.reset = panel_nt35510c_reset;
    nt35510c->base.init = panel_nt35510c_init;
    nt35510c->base.draw_bitmap = panel_nt35510c_draw_bitmap;
    nt35510c->base.invert_color = panel_nt35510c_invert_color;
    nt35510c->base.set_gap = panel_nt35510c_set_gap;
    nt35510c->base.mirror = panel_nt35510c_mirror;
    nt35510c->base.swap_xy = panel_nt35510c_swap_xy;
    nt35510c->base.disp_on_off = panel_nt35510c_disp_on_off;
    *ret_panel = &(nt35510c->base);
    ESP_LOGD(TAG, "new nt35510c panel @%p", nt35510c);

    return ESP_OK;

err:
    if (nt35510c) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(nt35510c);
    }
    return ret;
}

static esp_err_t panel_nt35510c_del(esp_lcd_panel_t *panel)
{
    nt35510c_panel_t *nt35510c = __containerof(panel, nt35510c_panel_t, base);

    if (nt35510c->reset_gpio_num >= 0) {
        gpio_reset_pin(nt35510c->reset_gpio_num);
    }
    ESP_LOGD(TAG, "del nt35510c panel @%p", nt35510c);
    free(nt35510c);
    return ESP_OK;
}

static esp_err_t panel_nt35510c_reset(esp_lcd_panel_t *panel)
{
    nt35510c_panel_t *nt35510c = __containerof(panel, nt35510c_panel_t, base);
    esp_lcd_panel_io_handle_t io = nt35510c->io;

    // perform hardware reset
    if (nt35510c->reset_gpio_num >= 0) {
        gpio_set_level(nt35510c->reset_gpio_num, nt35510c->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(nt35510c->reset_gpio_num, !nt35510c->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    } else {
        // perform software reset
        esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET << 8, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(20)); // spec, wait at least 5m before sending new command
    }

    return ESP_OK;
}

static esp_err_t panel_nt35510c_init(esp_lcd_panel_t *panel)
{
    nt35510c_panel_t *nt35510c = __containerof(panel, nt35510c_panel_t, base);
    esp_lcd_panel_io_handle_t io = nt35510c->io;
    // LCD goes into sleep mode and display will be turned off after power on reset, exit sleep mode first
    esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT << 8, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    // init1
    esp_lcd_panel_io_tx_param(io, (0xF000 << 8) + 0, (uint16_t[]) {
        0x55,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xF000 << 8) + 1, (uint16_t[]) {
        0xAA,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xF000 << 8) + 2, (uint16_t[]) {
        0x52,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xF000 << 8) + 3, (uint16_t[]) {
        0x08,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xF000 << 8) + 4, (uint16_t[]) {
        0x01,
    }, 2);

    // AVDD: manual
    esp_lcd_panel_io_tx_param(io, (0xB600 << 8) + 0, (uint16_t[]) {
        0x34,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB600 << 8) + 1, (uint16_t[]) {
        0x34,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB600 << 8) + 2, (uint16_t[]) {
        0x34,
    }, 2);


    esp_lcd_panel_io_tx_param(io, (0xB000 << 8) + 0, (uint16_t[]) {
        0x0D,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB000 << 8) + 1, (uint16_t[]) {
        0x0D,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB000 << 8) + 2, (uint16_t[]) {
        0x0D,
    }, 2);
    // AVEE: manual); LCD_WR_DATA
    esp_lcd_panel_io_tx_param(io, (0xB700 << 8) + 0, (uint16_t[]) {
        0x24,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB700 << 8) + 1, (uint16_t[]) {
        0x24,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB700 << 8) + 2, (uint16_t[]) {
        0x24,
    }, 2);

    esp_lcd_panel_io_tx_param(io, (0xB100 << 8) + 0, (uint16_t[]) {
        0x0D,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB100 << 8) + 1, (uint16_t[]) {
        0x0D,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB100 << 8) + 2, (uint16_t[]) {
        0x0D,
    }, 2);
    //#Power Control for
    //VCL
    esp_lcd_panel_io_tx_param(io, (0xB800 << 8) + 0, (uint16_t[]) {
        0x24,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB800 << 8) + 1, (uint16_t[]) {
        0x24,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB800 << 8) + 2, (uint16_t[]) {
        0x24,
    }, 2);

    esp_lcd_panel_io_tx_param(io, (0xB200 << 8) + 2, (uint16_t[]) {
        0x00,
    }, 2);
    // VGH: Clamp Enable); LCD_WR_DATA(
    esp_lcd_panel_io_tx_param(io, (0xB900 << 8) + 0, (uint16_t[]) {
        0x24,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB900 << 8) + 1, (uint16_t[]) {
        0x24,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB900 << 8) + 2, (uint16_t[]) {
        0x24,
    }, 2);

    esp_lcd_panel_io_tx_param(io, (0xB300 << 8) + 0, (uint16_t[]) {
        0x05,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB300 << 8) + 1, (uint16_t[]) {
        0x05,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB300 << 8) + 2, (uint16_t[]) {
        0x05,
    }, 2);
    // VGL(LVGL):
    esp_lcd_panel_io_tx_param(io, (0xBA00 << 8) + 0, (uint16_t[]) {
        0x34,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBA00 << 8) + 1, (uint16_t[]) {
        0x34,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBA00 << 8) + 2, (uint16_t[]) {
        0x34,
    }, 2);
    //# VGL_REG(VGLO)
    esp_lcd_panel_io_tx_param(io, (0xB500 << 8) + 0, (uint16_t[]) {
        0x0B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB500 << 8) + 1, (uint16_t[]) {
        0x0B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB500 << 8) + 2, (uint16_t[]) {
        0x0B,
    }, 2);
    //# VGMP/VGSP:
    esp_lcd_panel_io_tx_param(io, (0xBC00 << 8) + 0, (uint16_t[]) {
        0X00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBC00 << 8) + 1, (uint16_t[]) {
        0xA3,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBC00 << 8) + 2, (uint16_t[]) {
        0X00,
    }, 2);
    //# VGMN/VGSN
    esp_lcd_panel_io_tx_param(io, (0xBD00 << 8) + 0, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBD00 << 8) + 1, (uint16_t[]) {
        0xA3,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBD00 << 8) + 2, (uint16_t[]) {
        0x00,
    }, 2);
    //# VCOM=-0.1
    esp_lcd_panel_io_tx_param(io, (0xBE00 << 8) + 0, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBE00 << 8) + 1, (uint16_t[]) {
        0x63,
    }, 2);
    //#R+
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 0, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 1, (uint16_t[]) {
        0x37,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 2, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 3, (uint16_t[]) {
        0x52,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 4, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 5, (uint16_t[]) {
        0x7B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 6, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 7, (uint16_t[]) {
        0x99,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 8, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 9, (uint16_t[]) {
        0xB1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 10, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 11, (uint16_t[]) {
        0xD2,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 12, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 13, (uint16_t[]) {
        0xF6,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 14, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 15, (uint16_t[]) {
        0x27,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 16, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 17, (uint16_t[]) {
        0x4E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 18, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 19, (uint16_t[]) {
        0x8C,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 20, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 21, (uint16_t[]) {
        0xBE,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 22, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 23, (uint16_t[]) {
        0x0B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 24, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 25, (uint16_t[]) {
        0x48,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 26, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 27, (uint16_t[]) {
        0x4A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 28, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 29, (uint16_t[]) {
        0x7E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 30, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 31, (uint16_t[]) {
        0xBC,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 32, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 33, (uint16_t[]) {
        0xE1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 34, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 35, (uint16_t[]) {
        0x10,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 36, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 37, (uint16_t[]) {
        0x31,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 38, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 39, (uint16_t[]) {
        0x5A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 40, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 41, (uint16_t[]) {
        0x73,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 42, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 43, (uint16_t[]) {
        0x94,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 44, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 45, (uint16_t[]) {
        0x9F,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 46, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 47, (uint16_t[]) {
        0xB3,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 48, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 49, (uint16_t[]) {
        0xB9,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 50, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD100 << 8) + 51, (uint16_t[]) {
        0xC1,
    }, 2);
    // G+
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 0, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 1, (uint16_t[]) {
        0x37,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 2, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 3, (uint16_t[]) {
        0x52,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 4, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 5, (uint16_t[]) {
        0x7B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 6, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 7, (uint16_t[]) {
        0x99,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 8, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 9, (uint16_t[]) {
        0xB1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 10, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 11, (uint16_t[]) {
        0xD2,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 12, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 13, (uint16_t[]) {
        0xF6,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 14, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 15, (uint16_t[]) {
        0x27,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 16, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 17, (uint16_t[]) {
        0x4E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 18, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 19, (uint16_t[]) {
        0x8C,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 20, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 21, (uint16_t[]) {
        0xBE,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 22, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 23, (uint16_t[]) {
        0x0B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 24, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 25, (uint16_t[]) {
        0x48,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 26, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 27, (uint16_t[]) {
        0x4A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 28, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 29, (uint16_t[]) {
        0x7E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 30, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 31, (uint16_t[]) {
        0xBC,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 32, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 33, (uint16_t[]) {
        0xE1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 34, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 35, (uint16_t[]) {
        0x10,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 36, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 37, (uint16_t[]) {
        0x31,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 38, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 39, (uint16_t[]) {
        0x5A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 40, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 41, (uint16_t[]) {
        0x73,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 42, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 43, (uint16_t[]) {
        0x94,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 44, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 45, (uint16_t[]) {
        0x9F,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 46, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 47, (uint16_t[]) {
        0xB3,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 48, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 49, (uint16_t[]) {
        0xB9,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 50, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD200 << 8) + 51, (uint16_t[]) {
        0xC1,
    }, 2);
    // B+
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 0, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 1, (uint16_t[]) {
        0x37,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 2, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 3, (uint16_t[]) {
        0x52,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 4, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 5, (uint16_t[]) {
        0x7B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 6, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 7, (uint16_t[]) {
        0x99,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 8, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 9, (uint16_t[]) {
        0xB1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 10, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 11, (uint16_t[]) {
        0xD2,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 12, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 13, (uint16_t[]) {
        0xF6,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 14, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 15, (uint16_t[]) {
        0x27,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 16, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 17, (uint16_t[]) {
        0x4E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 18, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 19, (uint16_t[]) {
        0x8C,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 20, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 21, (uint16_t[]) {
        0xBE,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 22, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 23, (uint16_t[]) {
        0x0B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 24, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 25, (uint16_t[]) {
        0x48,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 26, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 27, (uint16_t[]) {
        0x4A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 28, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 29, (uint16_t[]) {
        0x7E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 30, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 31, (uint16_t[]) {
        0xBC,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 32, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 33, (uint16_t[]) {
        0xE1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 34, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 35, (uint16_t[]) {
        0x10,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 36, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 37, (uint16_t[]) {
        0x31,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 38, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 39, (uint16_t[]) {
        0x5A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 40, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 41, (uint16_t[]) {
        0x73,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 42, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 43, (uint16_t[]) {
        0x94,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 44, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 45, (uint16_t[]) {
        0x9F,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 46, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 47, (uint16_t[]) {
        0xB3,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 48, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 49, (uint16_t[]) {
        0xB9,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 50, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD300 << 8) + 51, (uint16_t[]) {
        0xC1,
    }, 2);
    // R-
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 0, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 1, (uint16_t[]) {
        0x37,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 2, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 3, (uint16_t[]) {
        0x52,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 4, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 5, (uint16_t[]) {
        0x7B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 6, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 7, (uint16_t[]) {
        0x99,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 8, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 9, (uint16_t[]) {
        0xB1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 10, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 11, (uint16_t[]) {
        0xD2,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 12, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 13, (uint16_t[]) {
        0xF6,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 14, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 15, (uint16_t[]) {
        0x27,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 16, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 17, (uint16_t[]) {
        0x4E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 18, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 19, (uint16_t[]) {
        0x8C,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 20, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 21, (uint16_t[]) {
        0xBE,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 22, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 23, (uint16_t[]) {
        0x0B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 24, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 25, (uint16_t[]) {
        0x48,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 26, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 27, (uint16_t[]) {
        0x4A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 28, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 29, (uint16_t[]) {
        0x7E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 30, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 31, (uint16_t[]) {
        0xBC,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 32, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 33, (uint16_t[]) {
        0xE1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 34, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 35, (uint16_t[]) {
        0x10,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 36, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 37, (uint16_t[]) {
        0x31,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 38, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 39, (uint16_t[]) {
        0x5A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 40, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 41, (uint16_t[]) {
        0x73,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 42, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 43, (uint16_t[]) {
        0x94,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 44, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 45, (uint16_t[]) {
        0x9F,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 46, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 47, (uint16_t[]) {
        0xB3,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 48, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 49, (uint16_t[]) {
        0xB9,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 50, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD400 << 8) + 51, (uint16_t[]) {
        0xC1,
    }, 2);
    // G-
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 0, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 1, (uint16_t[]) {
        0x37,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 2, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 3, (uint16_t[]) {
        0x52,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 4, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 5, (uint16_t[]) {
        0x7B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 6, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 7, (uint16_t[]) {
        0x99,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 8, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 9, (uint16_t[]) {
        0xB1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 10, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 11, (uint16_t[]) {
        0xD2,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 12, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 13, (uint16_t[]) {
        0xF6,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 14, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 15, (uint16_t[]) {
        0x27,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 16, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 17, (uint16_t[]) {
        0x4E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 18, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 19, (uint16_t[]) {
        0x8C,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 20, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 21, (uint16_t[]) {
        0xBE,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 22, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 23, (uint16_t[]) {
        0x0B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 24, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 25, (uint16_t[]) {
        0x48,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 26, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 27, (uint16_t[]) {
        0x4A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 28, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 29, (uint16_t[]) {
        0x7E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 30, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 31, (uint16_t[]) {
        0xBC,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 32, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 33, (uint16_t[]) {
        0xE1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 34, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 35, (uint16_t[]) {
        0x10,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 36, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 37, (uint16_t[]) {
        0x31,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 38, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 39, (uint16_t[]) {
        0x5A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 40, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 41, (uint16_t[]) {
        0x73,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 42, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 43, (uint16_t[]) {
        0x94,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 44, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 45, (uint16_t[]) {
        0x9F,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 46, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 47, (uint16_t[]) {
        0xB3,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 48, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 49, (uint16_t[]) {
        0xB9,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 50, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD500 << 8) + 51, (uint16_t[]) {
        0xC1,
    }, 2);
    // B-
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 0, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 1, (uint16_t[]) {
        0x37,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 2, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 3, (uint16_t[]) {
        0x52,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 4, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 5, (uint16_t[]) {
        0x7B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 6, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 7, (uint16_t[]) {
        0x99,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 8, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 9, (uint16_t[]) {
        0xB1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 10, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 11, (uint16_t[]) {
        0xD2,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 12, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 13, (uint16_t[]) {
        0xF6,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 14, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 15, (uint16_t[]) {
        0x27,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 16, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 17, (uint16_t[]) {
        0x4E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 18, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 19, (uint16_t[]) {
        0x8C,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 20, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 21, (uint16_t[]) {
        0xBE,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 22, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 23, (uint16_t[]) {
        0x0B,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 24, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 25, (uint16_t[]) {
        0x48,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 26, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 27, (uint16_t[]) {
        0x4A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 28, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 29, (uint16_t[]) {
        0x7E,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 30, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 31, (uint16_t[]) {
        0xBC,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 32, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 33, (uint16_t[]) {
        0xE1,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 34, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 35, (uint16_t[]) {
        0x10,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 36, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 37, (uint16_t[]) {
        0x31,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 38, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 39, (uint16_t[]) {
        0x5A,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 40, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 41, (uint16_t[]) {
        0x73,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 42, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 43, (uint16_t[]) {
        0x94,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 44, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 45, (uint16_t[]) {
        0x9F,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 46, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 47, (uint16_t[]) {
        0xB3,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 48, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 49, (uint16_t[]) {
        0xB9,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 50, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xD600 << 8) + 51, (uint16_t[]) {
        0xC1,
    }, 2);
    // #Enable Page0
    esp_lcd_panel_io_tx_param(io, (0xF000 << 8) + 0, (uint16_t[]) {
        0x55,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xF000 << 8) + 1, (uint16_t[]) {
        0xAA,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xF000 << 8) + 2, (uint16_t[]) {
        0x52,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xF000 << 8) + 3, (uint16_t[]) {
        0x08,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xF000 << 8) + 4, (uint16_t[]) {
        0x00,
    }, 2);
    //# RGB I/F Setting
    esp_lcd_panel_io_tx_param(io, (0xB000 << 8) + 0, (uint16_t[]) {
        0x08,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB000 << 8) + 1, (uint16_t[]) {
        0x05,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB000 << 8) + 2, (uint16_t[]) {
        0x02,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB000 << 8) + 3, (uint16_t[]) {
        0x05,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB000 << 8) + 4, (uint16_t[]) {
        0x02,
    }, 2);
    //## SDT:
    esp_lcd_panel_io_tx_param(io, (0xB600 << 8) + 0, (uint16_t[]) {
        0x08,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB500 << 8) + 0, (uint16_t[]) {
        0x50,
    }, 2);
    // ## Gate EQ:
    esp_lcd_panel_io_tx_param(io, (0xB700 << 8) + 0, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB700 << 8) + 1, (uint16_t[]) {
        0x00,
    }, 2);
    // ## Source EQ:
    esp_lcd_panel_io_tx_param(io, (0xB800 << 8) + 0, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB800 << 8) + 1, (uint16_t[]) {
        0x05,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB800 << 8) + 2, (uint16_t[]) {
        0x05,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xB800 << 8) + 3, (uint16_t[]) {
        0x05,
    }, 2);
    // Inversion: Column inversion (NVT)
    esp_lcd_panel_io_tx_param(io, (0xBC00 << 8) + 0, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBC00 << 8) + 1, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBC00 << 8) + 2, (uint16_t[]) {
        0x00,
    }, 2);
    // BOE's Setting(default)
    esp_lcd_panel_io_tx_param(io, (0xCC00 << 8) + 0, (uint16_t[]) {
        0x03,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xCC00 << 8) + 1, (uint16_t[]) {
        0x00,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xCC00 << 8) + 2, (uint16_t[]) {
        0x00,
    }, 2);
    // # Display Timing:
    esp_lcd_panel_io_tx_param(io, (0xBD00 << 8) + 0, (uint16_t[]) {
        0x01,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBD00 << 8) + 1, (uint16_t[]) {
        0x84,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBD00 << 8) + 2, (uint16_t[]) {
        0x07,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBD00 << 8) + 3, (uint16_t[]) {
        0x31,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xBD00 << 8) + 4, (uint16_t[]) {
        0x00,
    }, 2);

    esp_lcd_panel_io_tx_param(io, (0xBA00 << 8) + 0, (uint16_t[]) {
        0x01,
    }, 2);

    esp_lcd_panel_io_tx_param(io, (0xFF00 << 8) + 0, (uint16_t[]) {
        0xAA,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xFF00 << 8) + 1, (uint16_t[]) {
        0x55,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xFF00 << 8) + 2, (uint16_t[]) {
        0x25,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (0xFF00 << 8) + 3, (uint16_t[]) {
        0x01,
    }, 2);

    esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL << 8, (uint16_t[]) {
        nt35510c->madctl_val,
    }, 2);
    esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD << 8, (uint16_t[]) {
        nt35510c->colmod_cal,
    }, 2);

    return ESP_OK;
}

static esp_err_t panel_nt35510c_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    nt35510c_panel_t *nt35510c = __containerof(panel, nt35510c_panel_t, base);
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");
    esp_lcd_panel_io_handle_t io = nt35510c->io;

    x_start += nt35510c->x_gap;
    x_end += nt35510c->x_gap;
    y_start += nt35510c->y_gap;
    y_end += nt35510c->y_gap;

    // define an area of frame memory where MCU can access
    esp_lcd_panel_io_tx_param(io, (LCD_CMD_CASET << 8) + 0, (uint16_t[]) {
        (x_start >> 8) & 0xFF,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (LCD_CMD_CASET << 8) + 1, (uint16_t[]) {
        x_start & 0xFF,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (LCD_CMD_CASET << 8) + 2, (uint16_t[]) {
        ((x_end - 1) >> 8) & 0xFF,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (LCD_CMD_CASET << 8) + 3, (uint16_t[]) {
        (x_end - 1) & 0xFF,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (LCD_CMD_RASET << 8) + 0, (uint16_t[]) {
        (y_start >> 8) & 0xFF,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (LCD_CMD_RASET << 8) + 1, (uint16_t[]) {
        y_start & 0xFF,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (LCD_CMD_RASET << 8) + 2, (uint16_t[]) {
        ((y_end - 1) >> 8) & 0xFF,
    }, 2);
    esp_lcd_panel_io_tx_param(io, (LCD_CMD_RASET << 8) + 3, (uint16_t[]) {
        (y_end - 1) & 0xFF,
    }, 2);
    // transfer frame buffer
    size_t len = (x_end - x_start) * (y_end - y_start) * nt35510c->bits_per_pixel / 8;
    esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR << 8, color_data, len);

    return ESP_OK;
}

static esp_err_t panel_nt35510c_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    nt35510c_panel_t *nt35510c = __containerof(panel, nt35510c_panel_t, base);
    esp_lcd_panel_io_handle_t io = nt35510c->io;
    int command = 0;
    if (invert_color_data) {
        command = LCD_CMD_INVON;
    } else {
        command = LCD_CMD_INVOFF;
    }
    esp_lcd_panel_io_tx_param(io, command << 8, NULL, 0);
    return ESP_OK;
}

static esp_err_t panel_nt35510c_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    nt35510c_panel_t *nt35510c = __containerof(panel, nt35510c_panel_t, base);
    esp_lcd_panel_io_handle_t io = nt35510c->io;
    if (mirror_x) {
        nt35510c->madctl_val |= LCD_CMD_MX_BIT;
    } else {
        nt35510c->madctl_val &= ~LCD_CMD_MX_BIT;
    }
    if (mirror_y) {
        nt35510c->madctl_val |= LCD_CMD_MY_BIT;
    } else {
        nt35510c->madctl_val &= ~LCD_CMD_MY_BIT;
    }
    esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL << 8, (uint16_t[]) {
        nt35510c->madctl_val
    }, 2);
    return ESP_OK;
}

static esp_err_t panel_nt35510c_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    nt35510c_panel_t *nt35510c = __containerof(panel, nt35510c_panel_t, base);
    esp_lcd_panel_io_handle_t io = nt35510c->io;
    if (swap_axes) {
        nt35510c->madctl_val |= LCD_CMD_MV_BIT;
    } else {
        nt35510c->madctl_val &= ~LCD_CMD_MV_BIT;
    }
    esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL << 8, (uint16_t[]) {
        nt35510c->madctl_val
    }, 2);
    return ESP_OK;
}

static esp_err_t panel_nt35510c_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    nt35510c_panel_t *nt35510c = __containerof(panel, nt35510c_panel_t, base);
    nt35510c->x_gap = x_gap;
    nt35510c->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_nt35510c_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    nt35510c_panel_t *nt35510c = __containerof(panel, nt35510c_panel_t, base);
    esp_lcd_panel_io_handle_t io = nt35510c->io;
    int command = 0;
    if (on_off) {
        command = LCD_CMD_DISPON;
    } else {
        command = LCD_CMD_DISPOFF;
    }
    esp_lcd_panel_io_tx_param(io, command << 8, NULL, 0);
    return ESP_OK;
}
