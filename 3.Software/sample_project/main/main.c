#include <stdio.h>
#include "freertos/FreeRTOS.h" //RTOS实时系统
#include "freertos/task.h"     //RTOS实时系统
#include "esp_system.h"
#include <stdio.h>
#include "./Peripheral/peripheral.h"

void app_main(void)
{

    BEEP_Inint();

    while (1)
    {
        //BEEP
        ON_BEEP(1000);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        OFF_BEEP();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
