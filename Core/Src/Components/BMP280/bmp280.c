#include "bmp280.h"
#include "i2c.h"

/* SDO -> GND gives 0x76, SDO -> VDD gives 0x77 */
#define BMP280_ADDR (0x77 << 1)

#define REG_CALIB   0x88
#define REG_CTRL    0xF4
#define REG_CONFIG  0xF5
#define REG_DATA    0xF7

typedef struct
{
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
} bmp280_calib_t;

static bmp280_calib_t calib;

HAL_StatusTypeDef bmp280_init(void)
{
    uint8_t raw[24];
    uint8_t cfg;
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Read(&hi2c1, BMP280_ADDR, REG_CALIB,
                               I2C_MEMADD_SIZE_8BIT, raw, 24, HAL_MAX_DELAY);
    if (status != HAL_OK)
        return status;

    calib.dig_T1 = (uint16_t)(raw[1]  << 8 | raw[0]);
    calib.dig_T2 = (int16_t)(raw[3]  << 8 | raw[2]);
    calib.dig_T3 = (int16_t)(raw[5]  << 8 | raw[4]);
    calib.dig_P1 = (uint16_t)(raw[7]  << 8 | raw[6]);
    calib.dig_P2 = (int16_t)(raw[9]  << 8 | raw[8]);
    calib.dig_P3 = (int16_t)(raw[11] << 8 | raw[10]);
    calib.dig_P4 = (int16_t)(raw[13] << 8 | raw[12]);
    calib.dig_P5 = (int16_t)(raw[15] << 8 | raw[14]);
    calib.dig_P6 = (int16_t)(raw[17] << 8 | raw[16]);
    calib.dig_P7 = (int16_t)(raw[19] << 8 | raw[18]);
    calib.dig_P8 = (int16_t)(raw[21] << 8 | raw[20]);
    calib.dig_P9 = (int16_t)(raw[23] << 8 | raw[22]);

    /* temp x1, press x1, normal mode */
    cfg = 0x27;
    status = HAL_I2C_Mem_Write(&hi2c1, BMP280_ADDR, REG_CTRL,
                                I2C_MEMADD_SIZE_8BIT, &cfg, 1, HAL_MAX_DELAY);
    if (status != HAL_OK)
        return status;

    /* standby 62.5 ms, filter disabled */
    cfg = 0x00;
    return HAL_I2C_Mem_Write(&hi2c1, BMP280_ADDR, REG_CONFIG,
                              I2C_MEMADD_SIZE_8BIT, &cfg, 1, HAL_MAX_DELAY);
}

HAL_StatusTypeDef bmp280_read(float *temperature, float *pressure_hpa)
{
    uint8_t buf[6];
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Read(&hi2c1, BMP280_ADDR, REG_DATA,
                               I2C_MEMADD_SIZE_8BIT, buf, 6, HAL_MAX_DELAY);

    if (status != HAL_OK)
        return status;

    int32_t adc_P = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | (buf[2] >> 4);
    int32_t adc_T = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | (buf[5] >> 4);

    /* Compensation per the official Bosch formula (double, simplified for readability) */
    double var1, var2, t_fine, t, p;

    var1 = (((double)adc_T) / 16384.0 - ((double)calib.dig_T1) / 1024.0) * ((double)calib.dig_T2);
    var2 = ((((double)adc_T) / 131072.0 - ((double)calib.dig_T1) / 8192.0) *
            (((double)adc_T) / 131072.0 - ((double)calib.dig_T1) / 8192.0)) * ((double)calib.dig_T3);
    t_fine = var1 + var2;
    t = t_fine / 5120.0;
    *temperature = (float)t;

    var1 = (t_fine / 2.0) - 64000.0;
    var2 = var1 * var1 * ((double)calib.dig_P6) / 32768.0;
    var2 = var2 + var1 * ((double)calib.dig_P5) * 2.0;
    var2 = (var2 / 4.0) + (((double)calib.dig_P4) * 65536.0);
    var1 = (((double)calib.dig_P3) * var1 * var1 / 524288.0 + ((double)calib.dig_P2) * var1) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * ((double)calib.dig_P1);

    if (var1 == 0.0)
    {
        *pressure_hpa = 0.0f;
        return HAL_OK;
    }

    p = 1048576.0 - (double)adc_P;
    p = (p - (var2 / 4096.0)) * 6250.0 / var1;
    var1 = ((double)calib.dig_P9) * p * p / 2147483648.0;
    var2 = p * ((double)calib.dig_P8) / 32768.0;
    p = p + (var1 + var2 + ((double)calib.dig_P7)) / 16.0;

    *pressure_hpa = (float)(p / 100.0);

    return HAL_OK;
}
