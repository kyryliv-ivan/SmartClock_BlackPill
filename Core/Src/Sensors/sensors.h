/*
 * sensors.h
 *
 *  Created on: Aug 24, 2026
 *      Author: ivan
 */

#ifndef SRC_SENSORS_SENSORS_H_
#define SRC_SENSORS_SENSORS_H_

#include "main.h"

void sensors_init(void);
void sensors_poll(void);

float temperature_get(void);
float humidity_get(void);
float pressure_get(void);
float lux_get(void);

uint8_t hour_get(void);
uint8_t minute_get(void);
uint8_t second_get(void);
uint8_t day_get(void);
uint8_t month_get(void);
uint8_t year_get(void);

uint8_t rtc_ok_get(void);

HAL_StatusTypeDef time_set(uint8_t hours, uint8_t minutes, uint8_t day,
		uint8_t month, uint8_t year);

int32_t encoder_delta_get(void);
uint8_t encoder_tapped_get(void);

#endif /* SRC_SENSORS_SENSORS_H_ */
