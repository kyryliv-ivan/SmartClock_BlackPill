/*
 * sensors.c
 *
 *  Created on: Aug 24, 2026
 *      Author: ivan
 */

#include "sensors.h"
#include "main.h"
#include "bmp280.h"
#include "aht20.h"
#include "bh1750.h"

#define SENSOR_POLL_INTERVAL_MS 700
typedef enum {
	SENSOR_BMP280, SENSOR_AHT20, SENSOR_BH1750, SENSOR_COUNT
} sensor_step_t;

static float bmp_temp = 0.0f;
static float pressure = 0.0f;
static float aht_temp = 0.0f;
static float humidity = 0.0f;
static float lux = 0.0f;

static uint32_t sensor_tick = 0;
static uint32_t sensor_step = 0;

void sensors_init(void)
{
	bmp280_init();
	aht20_init();
	bh1750_init();
}

void sensors_poll(void)
{
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

