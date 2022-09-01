/*
The .c file is a peripheral, including the buzzer SD card button and other peripherals.
*/

#include <stdio.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_system.h"


#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (4) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (2048) // Set duty to 50%. ((2 ** 13) - 1) * 50% = 4095
#define LEDC_FREQUENCY          (800) // Frequency in Hertz. Set frequency at 5 kHz

// BEEP ,配置 定时器 和PWM
void BEEP_Inint()
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,          // LEDC speed speed_mode，高速模式或低速模式
        .timer_num = LEDC_TIMER,          //通道的定时器源（0 - 3）
        .duty_resolution = LEDC_DUTY_RES, // LEDC 通道占空比
        .freq_hz = LEDC_FREQUENCY,        // LEDC 定时器频率 (Hz)
        .clk_cfg = LEDC_AUTO_CLK          //从 ledc_clk_cfg_t 配置 LEDC 源时钟。请注意，LEDC_USE_RTC8M_CLK 和 LEDC_USE_XTAL_CLK 是非定时器特定的时钟源
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,        // LEDC speed speed_mode，高速模式或低速模式
        .channel = LEDC_CHANNEL,        // LEDC 通道 (0 - 7)
        .timer_sel = LEDC_TIMER,        //选择通道的定时器源 (0 - 3)
        .intr_type = LEDC_INTR_DISABLE, // 0
        .gpio_num = LEDC_OUTPUT_IO,     // GPIO引脚
        .duty = 0,                      // Set duty to 50%    //LEDC 通道占空比，占空比设置范围为 [0, (2**duty_resolution)]
        .hpoint = 0                     // LEDC channel hpoint value, the max value is 0xfffff
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

//Enlab BEEP  parameter Config
void ON_BEEP(unsigned int PWM_duty)
{
    //PWM_duty 0~4096 

    // Set duty to 50%
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 4096)); //设置PWM频率  模式 通道 频率
    // Update duty to apply the new valueUpdate duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL)); //设置PWM频率  模式 通道 频率

}

void OFF_BEEP()
{
    // Set duty to 50%
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0)); //设置PWM频率  模式 通道 频率
    // Update duty to apply the new valueUpdate duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL)); //设置PWM频率  模式 通道 频率
    
}


