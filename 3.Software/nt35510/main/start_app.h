#pragma once

#include "lvgl.h"

typedef struct
{
    // 时间
    lv_obj_t *label;
} start_app_t;

start_app_t *get_start_app(void);
void create_start_app(void);