#include "rest_server.h"

extern SemaphoreHandle_t xGuiSemaphore;

const static char *TAG = "restserver";

static esp_err_t status_update_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    if (total_len >= 256)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "POST data too long");
        return ESP_FAIL;
    }
    char *buf = calloc(sizeof(char), total_len + 1);
    int received = 0;

    while (cur_len < total_len)
    {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0)
        {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL)
    {
        httpd_resp_send_500(req);
        goto end;
    }

    cJSON *cpu_temp = cJSON_GetObjectItem(root, "tcpu");
    cJSON *gpu_temp = cJSON_GetObjectItem(root, "tgpu1dio");
    cJSON *cpu_use = cJSON_GetObjectItem(root, "scpuuti");
    cJSON *gpu_use = cJSON_GetObjectItem(root, "sgpu1uti");
    if (!cJSON_IsNumber(cpu_temp) || !cJSON_IsNumber(gpu_temp) || !cJSON_IsNumber(cpu_use) || !cJSON_IsNumber(gpu_use))
    {
        httpd_resp_send_500(req);
        goto end;
    }

    ESP_LOGI(TAG, "cpu_temp = %.2f, gpu_temp = %.2f, cpu_use = %.2f, gpu_use = %.2f", cpu_temp->valuedouble, gpu_temp->valuedouble, cpu_use->valuedouble, gpu_use->valuedouble);
    if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY))
    {
        lv_arc_set_value(get_label_app()->cpu_wendu_arc_bk5, (int16_t)cpu_temp->valuedouble);
        lv_arc_set_value(get_label_app()->gpu_wendu_arc_bk5, (int16_t)gpu_temp->valuedouble);
        lv_arc_set_value(get_label_app()->cpu_use_arc_bk5, (int16_t)cpu_use->valuedouble);
        lv_arc_set_value(get_label_app()->gpu_use_arc_bk5, (int16_t)gpu_use->valuedouble);

        lv_label_set_text_fmt(get_label_app()->cpu_wendu_bk5, "%.2f℃", cpu_temp->valuedouble);
        lv_label_set_text_fmt(get_label_app()->gpu_wendu_bk5, "%.2f℃", gpu_temp->valuedouble);
        lv_label_set_text_fmt(get_label_app()->cpu_use_bk5, "%.2f%%", cpu_use->valuedouble);
        lv_label_set_text_fmt(get_label_app()->gpu_use_bk5, "%.2f%%", gpu_use->valuedouble);
        xSemaphoreGive(xGuiSemaphore);
    }

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "Post control value successfully");

end:
    free(buf);
    return ESP_OK;
}

esp_err_t start_rest_server()
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.server_port = 8080;

    ESP_LOGI(TAG, "Starting HTTP Server");
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    /* URI handler for light brightness control */
    httpd_uri_t status_update_post_uri = {
        .uri = "/api/v1/status/update",
        .method = HTTP_POST,
        .handler = status_update_post_handler,
    };
    httpd_register_uri_handler(server, &status_update_post_uri);

    return ESP_OK;
}