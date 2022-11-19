#pragma once

#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl_gui.h"
#include "http_req.h"

#include "aht20.h"

#include "esp_http_client.h"

#include "time.h"
#include "cJSON.h"
#include "iot_button.h"

#include "esp_log.h"

#include "esp_netif.h"

#include "esp_sleep.h"

#define WEATHER_API_URL ""
#define WEATHER_KEY ""
#define WEATHER_CITY ""

typedef struct
{
    // 时间
    lv_obj_t *time_bk1;
    lv_obj_t *week_bk1;
    // 温湿度
    lv_obj_t *temp_bk3;
    lv_obj_t *hum_bk3;
    lv_obj_t *temp_label;
    lv_obj_t *hum_label;
    lv_obj_t *wendu_bk2;       // 实时天气大字
    lv_obj_t *wendu_small_bk2; // 下方小字

    lv_obj_t *cpu_wendu_arc_bk5; // CPU温度arc
    lv_obj_t *cpu_wendu_bk5;     // CPU温度
    lv_obj_t *gpu_wendu_arc_bk5; // GPU温度arc
    lv_obj_t *gpu_wendu_bk5;     // GPU温度

    lv_obj_t *cpu_use_arc_bk5; // GPU温度arc
    lv_obj_t *cpu_use_bk5;     // GPU温度
    lv_obj_t *gpu_use_arc_bk5; // GPU温度arc
    lv_obj_t *gpu_use_bk5;     // GPU温度

    uint8_t last_update_day;
} label_app_t;

label_app_t *get_label_app(void);
void create_label_app(void);