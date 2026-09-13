#include "bh1750.h"
#include "i2c.h"

/* All I2C calls below use a bounded 50ms timeout, not HAL_MAX_DELAY - a
   transient bus glitch (easy to trigger just by handling the sensor, e.g.
   testing Night Mode by covering/uncovering it) previously froze the whole
   main loop forever waiting on an I2C transfer that was never going to
   complete. A timed-out read here just skips one update instead. */

/* ADDR pin -> GND gives 0x23, ADDR pin -> VDD gives 0x5C */
#define BH1750_ADDR (0x23 << 1)

#define CMD_POWER_ON            0x01
#define CMD_RESET                0x07
#define CMD_CONT_H_RES_MODE     0x10  /* continuous, 1 lx resolution, ~120 ms per measurement */

/* Puts the sensor into continuous H-resolution mode. After this it keeps
   re-measuring on its own every ~120 ms - bh1750_read() can just be
   polled, no need to re-send the mode command each time. */
HAL_StatusTypeDef bh1750_init(void)
{
    uint8_t cmd;
    HAL_StatusTypeDef status;

    cmd = CMD_POWER_ON;
    status = HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, 50);
    if (status != HAL_OK)
        return status;

    cmd = CMD_RESET;
    status = HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, 50);
    if (status != HAL_OK)
        return status;

    cmd = CMD_CONT_H_RES_MODE;
    return HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, 50);
}

HAL_StatusTypeDef bh1750_read(float *lux)
{
    uint8_t buf[2];
    HAL_StatusTypeDef status;

    status = HAL_I2C_Master_Receive(&hi2c1, BH1750_ADDR, buf, 2, 50);
    if (status != HAL_OK)
        return status;

    uint16_t raw = ((uint16_t)buf[0] << 8) | buf[1];

    /* per datasheet: lux = raw / 1.2 (H-resolution mode) */
    *lux = raw / 1.2f;

    return HAL_OK;
}
