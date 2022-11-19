/*
 * @Author: NUOZHIHUI 1285574579@qq.com
 * @Date: 2022-07-16 14:11:49
 * @LastEditors: NUOZHIHUI 1285574579@qq.com
 * @LastEditTime: 2022-09-05 22:21:54
 * @FilePath: \nt35510-desktop-clock-master\main\lvgl_gui.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#pragma once

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "driver/ledc.h"

#include "demos/lv_demos.h"

#include "label_app.h"
#include "start_app.h"

#define lv_label_set_text_fmt_safe(obj, fmt, ...)               \
    if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) \
    {                                                           \
        lv_label_set_text_fmt(obj, fmt, ##__VA_ARGS__);         \
        xSemaphoreGive(xGuiSemaphore);                          \
    }

#define lv_label_set_text_safe(obj, ...)                        \
    if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) \
    {                                                           \
        lv_label_set_text(obj, ##__VA_ARGS__);                  \
        xSemaphoreGive(xGuiSemaphore);                          \
    }

// PCLK frequency can't go too high as the limitation of PSRAM bandwidth
#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)

#define LCD_BK_LIGHT_ON_LEVEL 1
#define LCD_BK_LIGHT_OFF_LEVEL !LCD_BK_LIGHT_ON_LEVEL

#define PIN_NUM_DATA0 18
#define PIN_NUM_DATA1 14
#define PIN_NUM_DATA2 17
#define PIN_NUM_DATA3 8
#define PIN_NUM_DATA4 39
#define PIN_NUM_DATA5 5
#define PIN_NUM_DATA6 38
#define PIN_NUM_DATA7 10

#define PIN_NUM_PCLK 12
#define PIN_NUM_CS 6
#define PIN_NUM_DC 7
#define PIN_NUM_RST 2
#define PIN_NUM_BK_LIGHT 3

// The pixel number in horizontal and vertical
#define LCD_H_RES 800
#define LCD_V_RES 480
// Bit number used to represent command and parameter
#define LCD_CMD_BITS 16
#define LCD_PARAM_BITS 16

#define LVGL_TICK_PERIOD_MS 2

// Supported alignment: 16, 32, 64. A higher alignment can enables higher burst transfer size, thus a higher i80 bus throughput.
#define PSRAM_DATA_ALIGNMENT 64

void guiTask(void *pvParameter);