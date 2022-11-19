#include "start_app.h"

LV_FONT_DECLARE(HarmonyOS_Sans_22)

static start_app_t local_app;

start_app_t *get_start_app(void)
{
    return &local_app;
}

void create_start_app(void)
{
    local_app.label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(local_app.label, &HarmonyOS_Sans_22, LV_PART_MAIN);
    lv_label_set_text(local_app.label, "系统启动中，请等待");
    lv_obj_set_align(local_app.label, LV_ALIGN_CENTER);
}