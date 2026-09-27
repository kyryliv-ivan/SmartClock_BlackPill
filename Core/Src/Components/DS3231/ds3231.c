/*
 * ds3231.c
 *
 *  Created on: Aug 25, 2026
 *      Author: ivan
 */

#include "ds3231.h"
#include "i2c.h"

static uint8_t to_bcd(uint8_t v)
{
	return ((v / 10) << 4) | (v % 10);
}

static uint8_t from_bcd(uint8_t v)
{
	return ((v >> 4) * 10) + (v & 0x0F);
}

HAL_StatusTypeDef rtc_read(Time *t, Date *d)
{
	uint8_t buf[7];
	HAL_StatusTypeDef status;

	status = HAL_I2C_Mem_Read(&hi2c1, DS3231_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, buf, 7, 50);
	if (status != HAL_OK)
		return status;

	t->seconds = from_bcd(buf[0] & 0x7F);
	t->minutes = from_bcd(buf[1] & 0x7F);
	t->hours = from_bcd(buf[2] & 0x3F);

	d->day = from_bcd(buf[4] & 0x3F);
	d->month = from_bcd(buf[5] & 0x1F);
	d->year = from_bcd(buf[6]);

	return HAL_OK;
}

HAL_StatusTypeDef rtc_set_time(Time t, Date d)
{
	uint8_t buf[7];

	buf[0] = to_bcd(t.seconds);
	buf[1] = to_bcd(t.minutes);
	buf[2] = to_bcd(t.hours);
	buf[3] = 1;
	buf[4] = to_bcd(d.day);
	buf[5] = to_bcd(d.month);
	buf[6] = to_bcd(d.year);

	return HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, buf, 7, 50);
}
