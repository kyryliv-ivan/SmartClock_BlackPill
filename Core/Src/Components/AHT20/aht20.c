#include "aht20.h"
#include "i2c.h"

#define AHT20_ADDR (0x38 << 1)

/* AHT20 needs >=40 ms after power-up before the first command (per datasheet) */
HAL_StatusTypeDef aht20_init(void)
{
    uint8_t cmd[3] = { 0xBE, 0x08, 0x00 };

    HAL_Delay(40);

    return HAL_I2C_Master_Transmit(&hi2c1, AHT20_ADDR, cmd, 3, HAL_MAX_DELAY);
}

HAL_StatusTypeDef aht20_read(float *temperature, float *humidity)
{
    uint8_t cmd[3] = { 0xAC, 0x33, 0x00 };
    uint8_t buf[6];
    HAL_StatusTypeDef status;

    status = HAL_I2C_Master_Transmit(&hi2c1, AHT20_ADDR, cmd, 3, HAL_MAX_DELAY);

    if (status != HAL_OK)
        return status;

    HAL_Delay(80);

    status = HAL_I2C_Master_Receive(&hi2c1, AHT20_ADDR, buf, 6, HAL_MAX_DELAY);

    if (status != HAL_OK)
        return status;

    uint32_t raw_hum  = ((uint32_t)buf[1] << 12) | ((uint32_t)buf[2] << 4) | (buf[3] >> 4);
    uint32_t raw_temp = (((uint32_t)buf[3] & 0x0F) << 16) | ((uint32_t)buf[4] << 8) | buf[5];

    *humidity    = (raw_hum  * 100.0f) / 1048576.0f;
    *temperature = (raw_temp * 200.0f) / 1048576.0f - 50.0f;

    return HAL_OK;
}
