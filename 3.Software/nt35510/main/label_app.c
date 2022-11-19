#include "label_app.h"

LV_FONT_DECLARE(HarmonyOS_Sans_22)
LV_FONT_DECLARE(HarmonyOS_Sans_48)

LV_IMG_DECLARE(hutao_0);
LV_IMG_DECLARE(hutao_1);
LV_IMG_DECLARE(hutao_2);
LV_IMG_DECLARE(hutao_3);
LV_IMG_DECLARE(hutao_4);
LV_IMG_DECLARE(hutao_5);
LV_IMG_DECLARE(hutao_6);
LV_IMG_DECLARE(hutao_7);
LV_IMG_DECLARE(hutao_8);
LV_IMG_DECLARE(hutao_9);

static const lv_img_dsc_t *hutao_anim_imgs[10] = {
    &hutao_0,
    &hutao_1,
    &hutao_2,
    &hutao_3,
    &hutao_4,
    &hutao_5,
    &hutao_6,
    &hutao_7,
    &hutao_8,
    &hutao_9,
};

extern SemaphoreHandle_t xGuiSemaphore;
extern esp_netif_t *sta_netif;
SemaphoreHandle_t xTLSSemaphore;

static label_app_t local_app;

static const char *TAG = "label_app";

label_app_t *get_label_app(void)
{
    return &local_app;
}

static void update_weather(void *pvParameters)
{
    ESP_LOGI(TAG, "Start update weather.");
    cJSON *weather_json = NULL, *air_json = NULL;
    uint8_t status = 0;
    char buf[1024];

    lv_label_set_text_safe(local_app.wendu_small_bk2, "天气加载中....");

    esp_http_client_config_t config = {
        .url = WEATHER_API_URL "/nowWeather?key=" WEATHER_KEY "&location=" WEATHER_CITY,
        .event_handler = _http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_data = buf,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = ESP_FAIL;
    if (pdTRUE == xSemaphoreTake(xTLSSemaphore, portMAX_DELAY))
    {
        // GET
        err = esp_http_client_perform(client);
        xSemaphoreGive(xTLSSemaphore);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        goto end;
    }

    // ESP_LOG_BUFFER_HEXDUMP(TAG, buf, esp_http_client_get_content_length(client), ESP_LOG_INFO);

    weather_json = cJSON_ParseWithLength(buf, esp_http_client_get_content_length(client));
    if (weather_json == NULL)
        goto end;
    cJSON *weather_json_now = cJSON_GetObjectItemCaseSensitive(weather_json, "now");
    if (!cJSON_IsObject(weather_json_now))
        goto end;

    cJSON *weather_text_obj = cJSON_GetObjectItemCaseSensitive(weather_json_now, "text");
    cJSON *weather_feelsLike_obj = cJSON_GetObjectItemCaseSensitive(weather_json_now, "feelsLike");
    cJSON *weather_humidity_obj = cJSON_GetObjectItemCaseSensitive(weather_json_now, "humidity");
    cJSON *weather_temp_obj = cJSON_GetObjectItemCaseSensitive(weather_json_now, "temp");
    cJSON *weather_wind_dir_obj = cJSON_GetObjectItemCaseSensitive(weather_json_now, "windDir");

    if (!cJSON_IsString(weather_text_obj) || !cJSON_IsString(weather_feelsLike_obj) || !cJSON_IsString(weather_humidity_obj) ||
        !cJSON_IsString(weather_temp_obj) || !cJSON_IsString(weather_wind_dir_obj))
    {
        goto end;
    }

    esp_http_client_set_url(client, WEATHER_API_URL "/nowAir?key=" WEATHER_KEY);
    if (pdTRUE == xSemaphoreTake(xTLSSemaphore, portMAX_DELAY))
    {
        err = esp_http_client_perform(client);
        xSemaphoreGive(xTLSSemaphore);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        goto end;
    }
    air_json = cJSON_ParseWithLength(buf, esp_http_client_get_content_length(client));
    if (air_json == NULL)
        goto end;

    cJSON *air_json_now = cJSON_GetObjectItemCaseSensitive(air_json, "now");
    if (!cJSON_IsObject(air_json_now))
        goto end;

    cJSON *air_category_obj = cJSON_GetObjectItemCaseSensitive(air_json_now, "category");
    if (!cJSON_IsString(air_category_obj))
        goto end;

    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];

    time(&now);
    localtime_r(&now, &timeinfo);

    // 格式化时间
    strftime(strftime_buf, sizeof(strftime_buf), "%H:%M:%S", &timeinfo);

    lv_label_set_text_fmt_safe(local_app.wendu_bk2, "%s℃", weather_temp_obj->valuestring);
    // 37/28℃\n多云 | 空气优\n更新时间: 18:36:01
    lv_label_set_text_fmt_safe(local_app.wendu_small_bk2, "体感温度%s℃ | 相对湿度%s%%\n%s | %s | 空气%s\n更新时间: %s",
                               weather_feelsLike_obj->valuestring, weather_humidity_obj->valuestring, weather_text_obj->valuestring,
                               weather_wind_dir_obj->valuestring, air_category_obj->valuestring, strftime_buf);
    status = 1;
end:
    if (!status)
        lv_label_set_text_safe(local_app.wendu_small_bk2, "天气更新失败,请手动更新");

    cJSON_Delete(weather_json);
    cJSON_Delete(air_json);
    esp_http_client_cleanup(client);

    vTaskDelete(NULL);
}

static void update_weather_cb(lv_timer_t *timer)
{
    xTaskCreatePinnedToCore(update_weather, "update_weather", 1024 * 6, NULL, 5, NULL, 1);
}

static void update_time_cb(lv_timer_t *timer)
{
    time_t now;
    struct tm timeinfo;
    char strftime_buf[128];

    time(&now);
    localtime_r(&now, &timeinfo);

    // 格式化时间
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d\n%H:%M:%S", &timeinfo);

    lv_label_set_text(local_app.time_bk1, strftime_buf);
}

static void update_time2_cb(lv_timer_t *timer)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char *date = "日";

    // 今天更新过不再更新
    if (timeinfo.tm_mday == local_app.last_update_day)
        return;
    local_app.last_update_day = timeinfo.tm_mday;

    switch (timeinfo.tm_wday)
    {
    case 0:
        date = "日";
        break;
    case 1:
        date = "一";
        break;
    case 2:
        date = "二";
        break;
    case 3:
        date = "三";
        break;
    case 4:
        date = "四";
        break;
    case 5:
        date = "五";
        break;
    case 6:
        date = "六";
        break;

    default:
        break;
    }

    // int time_left = (1652490000 - now) / 60 / 60 / 24;

    lv_label_set_text_fmt(local_app.week_bk1, "星期%s", date);
}

static void update_envirment(void *pvParameters)
{
    float humidity, temperature;
    aht20_read_data(&humidity, &temperature);

    lv_label_set_text_fmt_safe(local_app.hum_label, "湿度: %.2f%%", humidity);
    lv_label_set_text_fmt_safe(local_app.temp_label, "温度: %.2f℃", temperature);
    if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY))
    {
        lv_arc_set_value(local_app.hum_bk3, (int16_t)humidity);
        lv_arc_set_value(local_app.temp_bk3, (int16_t)temperature);
        xSemaphoreGive(xGuiSemaphore);
    }

    vTaskDelete(NULL);
}

static void update_envirment_cb(lv_timer_t *timer)
{
    xTaskCreatePinnedToCore(update_envirment, "update_envirment", 1024 * 4, NULL, 5, NULL, 1);
}

static void button_shutdown_click_cb(void *arg)
{
    esp_deep_sleep_start();
}

void create_label_app(void)
{
    xTLSSemaphore = xSemaphoreCreateMutex();

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
    lv_obj_remove_style_all(scr);

    static lv_style_t border_style;
    if (border_style.sentinel != LV_STYLE_SENTINEL_VALUE)
    {
        lv_style_init(&border_style);
        lv_style_set_border_side(&border_style, LV_BORDER_SIDE_FULL);
        lv_style_set_border_opa(&border_style, LV_OPA_COVER);
        lv_style_set_border_width(&border_style, 2);
        lv_style_set_border_color(&border_style, lv_color_black());
        lv_style_set_radius(&border_style, 10);
    }

    static lv_style_t title_style;
    if (title_style.sentinel != LV_STYLE_SENTINEL_VALUE)
    {
        lv_style_init(&title_style);
        lv_style_set_text_font(&title_style, &HarmonyOS_Sans_22);
        lv_style_set_bg_opa(&title_style, LV_OPA_COVER);
        lv_style_set_pad_ver(&title_style, 3);
        lv_style_set_pad_hor(&title_style, 15);
        lv_style_set_text_color(&title_style, lv_color_white());
        lv_style_set_radius(&title_style, 5);
    }

    // BLOCK1 左上
    lv_obj_t *block1 = lv_obj_create(scr);
    lv_obj_remove_style_all(block1);
    lv_obj_add_style(block1, &border_style, LV_PART_MAIN);
    lv_obj_set_size(block1, 320, 230);
    lv_obj_set_pos(block1, 5, 5);

    lv_obj_t *label_bk1 = lv_label_create(block1);
    lv_label_set_text(label_bk1, "系统时间");
    lv_obj_align(label_bk1, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(label_bk1, lv_color_hex(0xFC814A), LV_PART_MAIN);
    lv_obj_add_style(label_bk1, &title_style, LV_PART_MAIN);

    local_app.time_bk1 = lv_label_create(block1);
    lv_obj_set_style_text_align(local_app.time_bk1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    // lv_obj_set_width(time_bk1, lv_pct(90));
    lv_label_set_text(local_app.time_bk1, "2022-01-01\n00:00:00");
    lv_obj_set_style_text_font(local_app.time_bk1, &HarmonyOS_Sans_48, LV_PART_MAIN);
    lv_obj_align(local_app.time_bk1, LV_ALIGN_TOP_MID, 0, 50);

    local_app.week_bk1 = lv_label_create(block1);
    lv_obj_set_style_text_align(local_app.week_bk1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(local_app.week_bk1, "星期一");
    lv_obj_set_style_text_font(local_app.week_bk1, &HarmonyOS_Sans_48, LV_PART_MAIN);
    lv_obj_align(local_app.week_bk1, LV_ALIGN_TOP_MID, 0, 155);
    lv_obj_set_style_bg_color(local_app.week_bk1, lv_color_hex(0x610F7F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(local_app.week_bk1, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(local_app.week_bk1, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(local_app.week_bk1, 7, LV_PART_MAIN);
    lv_obj_set_style_radius(local_app.week_bk1, 12, LV_PART_MAIN);
    lv_obj_set_style_text_color(local_app.week_bk1, lv_color_white(), LV_PART_MAIN);

    // BLOCK2 左下
    lv_obj_t *block2 = lv_obj_create(scr);
    lv_obj_remove_style_all(block2);
    lv_obj_add_style(block2, &border_style, LV_PART_MAIN);
    lv_obj_set_size(block2, 320, 230);
    lv_obj_set_pos(block2, 5, 240);

    lv_obj_t *label_bk2 = lv_label_create(block2);
    lv_label_set_text(label_bk2, "实时天气");
    lv_obj_align(label_bk2, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(label_bk2, lv_color_hex(0xB80C09), LV_PART_MAIN);
    lv_obj_add_style(label_bk2, &title_style, LV_PART_MAIN);

    lv_obj_t *chenshi_bk2 = lv_label_create(block2);
    lv_label_set_text(chenshi_bk2, "奎文区");
    lv_obj_align(chenshi_bk2, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_text_font(chenshi_bk2, &HarmonyOS_Sans_22, LV_PART_MAIN);

    local_app.wendu_bk2 = lv_label_create(block2);
    lv_label_set_text(local_app.wendu_bk2, "23℃");
    lv_obj_set_style_text_font(local_app.wendu_bk2, &HarmonyOS_Sans_48, LV_PART_MAIN);
    lv_obj_align(local_app.wendu_bk2, LV_ALIGN_TOP_MID, 0, 70);

    local_app.wendu_small_bk2 = lv_label_create(block2);
    lv_label_set_text(local_app.wendu_small_bk2, "32/21℃\n多云 | 空气优\n更新时间: 18:36:01");
    lv_obj_set_style_text_align(local_app.wendu_small_bk2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(local_app.wendu_small_bk2, &HarmonyOS_Sans_22, LV_PART_MAIN);
    lv_obj_align(local_app.wendu_small_bk2, LV_ALIGN_TOP_MID, 0, 121);

    // BLOCL3 右上1
    lv_obj_t *block3 = lv_obj_create(scr);
    lv_obj_remove_style_all(block3);
    lv_obj_add_style(block3, &border_style, LV_PART_MAIN);
    lv_obj_set_size(block3, 190, 230);
    lv_obj_set_pos(block3, 330, 5);

    lv_obj_t *label_bk3 = lv_label_create(block3);
    lv_label_set_text(label_bk3, "环境检测");
    lv_obj_align(label_bk3, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(label_bk3, lv_color_hex(0x094074), LV_PART_MAIN);
    lv_obj_add_style(label_bk3, &title_style, LV_PART_MAIN);

    /*Create an Arc*/
    local_app.temp_bk3 = lv_arc_create(block3);
    lv_obj_set_size(local_app.temp_bk3, 90, 90);
    lv_arc_set_rotation(local_app.temp_bk3, 135);
    lv_arc_set_bg_angles(local_app.temp_bk3, 0, 270);
    lv_arc_set_value(local_app.temp_bk3, 26);
    lv_obj_remove_style(local_app.temp_bk3, NULL, LV_PART_KNOB);  /*Be sure the knob is not displayed*/
    lv_obj_clear_flag(local_app.temp_bk3, LV_OBJ_FLAG_CLICKABLE); /*To not allow adjusting by click*/
    lv_obj_set_pos(local_app.temp_bk3, 3, 50);
    lv_obj_set_style_arc_color(local_app.temp_bk3, lv_color_hex(0x003F91), LV_PART_INDICATOR);

    local_app.temp_label = lv_label_create(local_app.temp_bk3);
    lv_label_set_text(local_app.temp_label, "温度");
    lv_obj_set_align(local_app.temp_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(local_app.temp_label, &HarmonyOS_Sans_22, LV_PART_MAIN);

    local_app.temp_label = lv_label_create(block3);
    lv_label_set_text(local_app.temp_label, "温度: 30.7℃");
    lv_obj_align(local_app.temp_label, LV_ALIGN_TOP_MID, 5, 150);
    lv_obj_set_width(local_app.temp_label, lv_pct(90));
    lv_obj_set_style_text_font(local_app.temp_label, &HarmonyOS_Sans_22, LV_PART_MAIN);

    /*Create an Arc*/
    local_app.hum_bk3 = lv_arc_create(block3);
    lv_obj_set_size(local_app.hum_bk3, 90, 90);
    lv_arc_set_rotation(local_app.hum_bk3, 135);
    lv_arc_set_bg_angles(local_app.hum_bk3, 0, 270);
    lv_arc_set_value(local_app.hum_bk3, 40);
    lv_obj_remove_style(local_app.hum_bk3, NULL, LV_PART_KNOB);  /*Be sure the knob is not displayed*/
    lv_obj_clear_flag(local_app.hum_bk3, LV_OBJ_FLAG_CLICKABLE); /*To not allow adjusting by click*/
    lv_obj_set_pos(local_app.hum_bk3, 96, 50);
    lv_obj_set_style_arc_color(local_app.hum_bk3, lv_color_hex(0x809BCE), LV_PART_INDICATOR);

    local_app.hum_label = lv_label_create(local_app.hum_bk3);
    lv_label_set_text(local_app.hum_label, "湿度");
    lv_obj_set_align(local_app.hum_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(local_app.hum_label, &HarmonyOS_Sans_22, LV_PART_MAIN);

    local_app.hum_label = lv_label_create(block3);
    lv_label_set_text(local_app.hum_label, "湿度: 30%");
    lv_obj_align(local_app.hum_label, LV_ALIGN_TOP_MID, 5, 180);
    lv_obj_set_width(local_app.hum_label, lv_pct(90));
    lv_obj_set_style_text_font(local_app.hum_label, &HarmonyOS_Sans_22, LV_PART_MAIN);

    // BLOCK4 右上2
    lv_obj_t *block4 = lv_obj_create(scr);
    lv_obj_remove_style_all(block4);
    lv_obj_add_style(block4, &border_style, LV_PART_MAIN);
    lv_obj_set_size(block4, 260, 230);
    lv_obj_set_pos(block4, 525, 5);

    // lv_obj_t *label_bk4 = lv_label_create(block4);
    // lv_label_set_text(label_bk4, "测试标题4");
    // lv_obj_align(label_bk4, LV_ALIGN_TOP_MID, 0, 5);
    // lv_obj_set_style_bg_color(label_bk4, lv_color_hex(0x79B473), LV_PART_MAIN);
    // lv_obj_add_style(label_bk4, &title_style, LV_PART_MAIN);

    lv_obj_t *animimg_bk4 = lv_animimg_create(block4);
    lv_obj_center(animimg_bk4);
    lv_animimg_set_src(animimg_bk4, (lv_img_dsc_t **)hutao_anim_imgs, 10);
    lv_animimg_set_duration(animimg_bk4, 2100);
    lv_animimg_set_repeat_count(animimg_bk4, LV_ANIM_REPEAT_INFINITE);
    lv_animimg_start(animimg_bk4);
    lv_obj_set_align(animimg_bk4, LV_ALIGN_CENTER);

    // BLOCK5 右下
    lv_obj_t *block5 = lv_obj_create(scr);
    lv_obj_remove_style_all(block5);
    lv_obj_add_style(block5, &border_style, LV_PART_MAIN);
    lv_obj_set_size(block5, 455, 230);
    lv_obj_set_pos(block5, 330, 240);

    lv_obj_t *label_bk5 = lv_label_create(block5);
    lv_label_set_text(label_bk5, "状态监视器");
    lv_obj_align(label_bk5, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(label_bk5, lv_color_hex(0xB497D6), LV_PART_MAIN);
    lv_obj_add_style(label_bk5, &title_style, LV_PART_MAIN);

    // CPU温度
    lv_obj_t *parent_bk5 = lv_obj_create(block5);
    lv_obj_remove_style_all(parent_bk5);
    lv_obj_set_size(parent_bk5, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    /*Create an Arc*/
    local_app.cpu_wendu_arc_bk5 = lv_arc_create(parent_bk5);
    lv_obj_set_size(local_app.cpu_wendu_arc_bk5, 90, 90);
    lv_arc_set_rotation(local_app.cpu_wendu_arc_bk5, 135);
    lv_arc_set_bg_angles(local_app.cpu_wendu_arc_bk5, 0, 270);
    lv_arc_set_value(local_app.cpu_wendu_arc_bk5, 80);
    lv_obj_remove_style(local_app.cpu_wendu_arc_bk5, NULL, LV_PART_KNOB);  /*Be sure the knob is not displayed*/
    lv_obj_clear_flag(local_app.cpu_wendu_arc_bk5, LV_OBJ_FLAG_CLICKABLE); /*To not allow adjusting by click*/
    lv_obj_set_style_arc_color(local_app.cpu_wendu_arc_bk5, lv_color_hex(0xFFA8A9), LV_PART_INDICATOR);

    local_app.cpu_wendu_bk5 = lv_label_create(local_app.cpu_wendu_arc_bk5);
    lv_label_set_text(local_app.cpu_wendu_bk5, "CPU\n温度");
    lv_obj_set_align(local_app.cpu_wendu_bk5, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(local_app.cpu_wendu_bk5, &HarmonyOS_Sans_22, LV_PART_MAIN);

    local_app.cpu_wendu_bk5 = lv_label_create(parent_bk5);
    lv_label_set_text(local_app.cpu_wendu_bk5, "80℃");
    lv_obj_align_to(local_app.cpu_wendu_bk5, local_app.cpu_wendu_arc_bk5, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_align(local_app.cpu_wendu_bk5, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(local_app.cpu_wendu_bk5, 90);
    lv_obj_set_style_text_font(local_app.cpu_wendu_bk5, &HarmonyOS_Sans_22, LV_PART_MAIN);

    lv_obj_align(parent_bk5, LV_ALIGN_TOP_LEFT, 20, 60);
    // GPU温度
    parent_bk5 = lv_obj_create(block5);
    lv_obj_remove_style_all(parent_bk5);
    lv_obj_set_size(parent_bk5, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    /*Create an Arc*/
    local_app.gpu_wendu_arc_bk5 = lv_arc_create(parent_bk5);
    lv_obj_set_size(local_app.gpu_wendu_arc_bk5, 90, 90);
    lv_arc_set_rotation(local_app.gpu_wendu_arc_bk5, 135);
    lv_arc_set_bg_angles(local_app.gpu_wendu_arc_bk5, 0, 270);
    lv_arc_set_value(local_app.gpu_wendu_arc_bk5, 100);
    lv_obj_remove_style(local_app.gpu_wendu_arc_bk5, NULL, LV_PART_KNOB);  /*Be sure the knob is not displayed*/
    lv_obj_clear_flag(local_app.gpu_wendu_arc_bk5, LV_OBJ_FLAG_CLICKABLE); /*To not allow adjusting by click*/
    lv_obj_set_style_arc_color(local_app.gpu_wendu_arc_bk5, lv_color_hex(0x53917E), LV_PART_INDICATOR);

    local_app.gpu_wendu_bk5 = lv_label_create(local_app.gpu_wendu_arc_bk5);
    lv_label_set_text(local_app.gpu_wendu_bk5, "GPU\n温度");
    lv_obj_set_align(local_app.gpu_wendu_bk5, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(local_app.gpu_wendu_bk5, &HarmonyOS_Sans_22, LV_PART_MAIN);

    local_app.gpu_wendu_bk5 = lv_label_create(parent_bk5);
    lv_label_set_text(local_app.gpu_wendu_bk5, "100℃");
    lv_obj_align_to(local_app.gpu_wendu_bk5, local_app.gpu_wendu_arc_bk5, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_align(local_app.gpu_wendu_bk5, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(local_app.gpu_wendu_bk5, 90);
    lv_obj_set_style_text_font(local_app.gpu_wendu_bk5, &HarmonyOS_Sans_22, LV_PART_MAIN);

    lv_obj_align(parent_bk5, LV_ALIGN_TOP_LEFT, 120, 60);
    // CPU占用
    parent_bk5 = lv_obj_create(block5);
    lv_obj_remove_style_all(parent_bk5);
    lv_obj_set_size(parent_bk5, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    /*Create an Arc*/
    local_app.cpu_use_arc_bk5 = lv_arc_create(parent_bk5);
    lv_obj_set_size(local_app.cpu_use_arc_bk5, 90, 90);
    lv_arc_set_rotation(local_app.cpu_use_arc_bk5, 135);
    lv_arc_set_bg_angles(local_app.cpu_use_arc_bk5, 0, 270);
    lv_arc_set_value(local_app.cpu_use_arc_bk5, 35);
    lv_obj_remove_style(local_app.cpu_use_arc_bk5, NULL, LV_PART_KNOB);  /*Be sure the knob is not displayed*/
    lv_obj_clear_flag(local_app.cpu_use_arc_bk5, LV_OBJ_FLAG_CLICKABLE); /*To not allow adjusting by click*/
    lv_obj_set_style_arc_color(local_app.cpu_use_arc_bk5, lv_color_hex(0xA04668), LV_PART_INDICATOR);

    local_app.cpu_use_bk5 = lv_label_create(local_app.cpu_use_arc_bk5);
    lv_label_set_text(local_app.cpu_use_bk5, "CPU\n占用");
    lv_obj_set_align(local_app.cpu_use_bk5, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(local_app.cpu_use_bk5, &HarmonyOS_Sans_22, LV_PART_MAIN);

    local_app.cpu_use_bk5 = lv_label_create(parent_bk5);
    lv_label_set_text(local_app.cpu_use_bk5, "80%");
    lv_obj_align_to(local_app.cpu_use_bk5, local_app.cpu_use_arc_bk5, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_align(local_app.cpu_use_bk5, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(local_app.cpu_use_bk5, 90);
    lv_obj_set_style_text_font(local_app.cpu_use_bk5, &HarmonyOS_Sans_22, LV_PART_MAIN);

    lv_obj_align(parent_bk5, LV_ALIGN_TOP_LEFT, 220, 60);

    // GPU占用
    parent_bk5 = lv_obj_create(block5);
    lv_obj_remove_style_all(parent_bk5);
    lv_obj_set_size(parent_bk5, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    /*Create an Arc*/
    local_app.gpu_use_arc_bk5 = lv_arc_create(parent_bk5);
    lv_obj_set_size(local_app.gpu_use_arc_bk5, 90, 90);
    lv_arc_set_rotation(local_app.gpu_use_arc_bk5, 135);
    lv_arc_set_bg_angles(local_app.gpu_use_arc_bk5, 0, 270);
    lv_arc_set_value(local_app.gpu_use_arc_bk5, 78);
    lv_obj_remove_style(local_app.gpu_use_arc_bk5, NULL, LV_PART_KNOB);  /*Be sure the knob is not displayed*/
    lv_obj_clear_flag(local_app.gpu_use_arc_bk5, LV_OBJ_FLAG_CLICKABLE); /*To not allow adjusting by click*/
    lv_obj_set_style_arc_color(local_app.gpu_use_arc_bk5, lv_color_hex(0x6EA4BF), LV_PART_INDICATOR);

    local_app.gpu_use_bk5 = lv_label_create(local_app.gpu_use_arc_bk5);
    lv_label_set_text(local_app.gpu_use_bk5, "GPU\n占用");
    lv_obj_set_align(local_app.gpu_use_bk5, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(local_app.gpu_use_bk5, &HarmonyOS_Sans_22, LV_PART_MAIN);

    local_app.gpu_use_bk5 = lv_label_create(parent_bk5);
    lv_label_set_text(local_app.gpu_use_bk5, "100%");
    lv_obj_align_to(local_app.gpu_use_bk5, local_app.gpu_use_arc_bk5, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_align(local_app.gpu_use_bk5, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(local_app.gpu_use_bk5, 90);
    lv_obj_set_style_text_font(local_app.gpu_use_bk5, &HarmonyOS_Sans_22, LV_PART_MAIN);

    lv_obj_align(parent_bk5, LV_ALIGN_TOP_LEFT, 320, 60);
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(sta_netif, &ip_info);

    lv_obj_t *ip_addr_bk5 = lv_label_create(block5);
    lv_label_set_text_fmt(ip_addr_bk5, IPSTR, IP2STR(&ip_info.ip));
    lv_obj_set_style_text_font(ip_addr_bk5, &HarmonyOS_Sans_22, LV_PART_MAIN);
    lv_obj_align(ip_addr_bk5, LV_ALIGN_TOP_MID, 0, 180);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_OUT, 800, 0, true);

    lv_timer_t *timer = lv_timer_create(update_time_cb, 500, NULL);
    lv_timer_ready(timer);

    lv_timer_t *timer2 = lv_timer_create(update_time2_cb, 30000, NULL);
    lv_timer_ready(timer2);

    // lv_timer_t *timer3 = lv_timer_create(update_weather_cb, 1200000, NULL);
    // lv_timer_ready(timer3);

    // lv_timer_t *timer4 = lv_timer_create(update_envirment_cb, 30000, NULL);
    // lv_timer_ready(timer4);

    button_config_t gpio_btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = GPIO_NUM_0,
            .active_level = 0,
        },
    };
    button_handle_t gpio_btn = iot_button_create(&gpio_btn_cfg);
    iot_button_register_cb(gpio_btn, BUTTON_DOUBLE_CLICK, (button_cb_t)update_weather_cb);
    iot_button_register_cb(gpio_btn, BUTTON_LONG_PRESS_START, (button_cb_t)button_shutdown_click_cb);
}