#include "aht20.h"

void aht20_init(void)
{
    uint8_t write_buf[3] = {0x00};
    write_buf[0] = 0xba;
    i2c_master_write_to_device(I2C_NUM_0, AHT20_ADDR, write_buf, 1, pdMS_TO_TICKS(AHT20_TIMEOUT_MS));
    vTaskDelay(pdMS_TO_TICKS(20));
    write_buf[0] = 0xbe;
    write_buf[1] = 0x08;
    write_buf[2] = 0x00;
    i2c_master_write_to_device(I2C_NUM_0, AHT20_ADDR, write_buf, 3, pdMS_TO_TICKS(AHT20_TIMEOUT_MS));
}

void aht20_read_data(float *humidity, float *temperature)
{
    uint8_t write_buf[3] = {0xac, 0x33, 0x00};
    i2c_master_write_to_device(I2C_NUM_0, AHT20_ADDR, write_buf, 3, pdMS_TO_TICKS(AHT20_TIMEOUT_MS));
    vTaskDelay(pdMS_TO_TICKS(75));
    uint8_t read_buffer[6] = {0x00};
    i2c_master_read_from_device(I2C_NUM_0, AHT20_ADDR, read_buffer, 6, pdMS_TO_TICKS(AHT20_TIMEOUT_MS));

    uint32_t humidity_raw = (read_buffer[1] << 12) | (read_buffer[2] << 4) | (read_buffer[3] >> 4);
    *humidity = humidity_raw / 1048576.0 * 100;

    uint32_t temp_raw = ((read_buffer[3] & 0x0f) << 16) | read_buffer[4] << 8 | read_buffer[5];
    *temperature = temp_raw / 1048576.0 * 200 - 50;
}