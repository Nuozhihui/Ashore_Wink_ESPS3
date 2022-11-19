#pragma once

#include <stdio.h>
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

esp_err_t _http_event_handler(esp_http_client_event_t *evt);