/*
 * sensors.c
 *
 *  Created on: Aug 24, 2026
 *      Author: ivan
 */

#include "sensors.h"
#include "main.h"
#include "ds3231.h"
#include "bmp280.h"
#include "aht20.h"
#include "bh1750.h"
#include <string.h>

#define SENSOR_POLL_INTERVAL_MS 700
#define RTC_POLL_INTERVAL_MS    200
#define RTC_ERROR_TIMEOUT_MS    3000

typedef enum {
	SENSOR_BMP280, SENSOR_AHT20, SENSOR_BH1750, SENSOR_COUNT
} sensor_step_t;

static float bmp_temp = 0.0f;
static float pressure = 0.0f;
static float aht_temp = 0.0f;
static float humidity = 0.0f;
static float lux = 0.0f;

static uint32_t sensor_tick = 0;
static uint32_t sensor_step = SENSOR_BMP280;

static Time rtc_time = { 0 };
static Date rtc_date = { 0 };
static uint32_t rtc_tick = 0;
static uint32_t rtc_last_good = 0;

void sensors_init(void)
{
	bmp280_init();
	aht20_init();
	bh1750_init();
}

void sensors_poll(void)
{

	if (HAL_GetTick() - rtc_tick >= RTC_POLL_INTERVAL_MS)
	{
		rtc_tick = HAL_GetTick();

		Time t;
		Date d;
		if (rtc_read(&t, &d) == HAL_OK && t.hours < 24 && t.minutes < 60
				&& t.seconds < 60 && d.day >= 1 && d.day <= 31 && d.month >= 1
				&& d.month <= 12 && d.year <= 99)
		{
			rtc_time = t;
			rtc_date = d;
			rtc_last_good = HAL_GetTick();
		}
	}

	if (HAL_GetTick() - sensor_tick < SENSOR_POLL_INTERVAL_MS)
		return;
	sensor_tick = HAL_GetTick();

	switch (sensor_step)
	{
	case SENSOR_BMP280:
		bmp280_read(&bmp_temp, &pressure);
		break;
	case SENSOR_AHT20:
		aht20_read(&aht_temp, &humidity);
		break;
	case SENSOR_BH1750:
		bh1750_read(&lux);
		break;
	}
	sensor_step = (sensor_step + 1) % SENSOR_COUNT;
}

float temperature_get(void)
{
	return aht_temp;
}

float humidity_get(void)
{
	return humidity;
}

float pressure_get(void)
{
	return pressure;
}

float lux_get(void)
{
	return lux;
}

uint8_t hour_get(void)
{
	return rtc_time.hours;
}

uint8_t minute_get(void)
{
	return rtc_time.minutes;
}

uint8_t second_get(void)
{
	return rtc_time.seconds;
}

uint8_t day_get(void)
{
	return rtc_date.day;
}

uint8_t month_get(void)
{
	return rtc_date.month;
}

uint8_t year_get(void)
{
	return rtc_date.year;
}

uint8_t rtc_ok_get(void)
{
	return (HAL_GetTick() - rtc_last_good) < RTC_ERROR_TIMEOUT_MS;
}

HAL_StatusTypeDef time_set(uint8_t hours, uint8_t minutes,
                            uint8_t day, uint8_t month, uint8_t year)
{
	Time t = { .hours = hours, .minutes = minutes, .seconds = 0 };
	Date d = { .day = day, .month = month, .year = year };

	HAL_StatusTypeDef status = HAL_ERROR;
	for (uint8_t attempt = 0; attempt < 5; attempt++)
	{
		status = rtc_set_time(t, d);
		if (status == HAL_OK)
			break;
		HAL_Delay(20);
	}

	if (status == HAL_OK)
	{
		rtc_time = t;
		rtc_date = d;
		rtc_last_good = HAL_GetTick();
	}
	return status;
}
