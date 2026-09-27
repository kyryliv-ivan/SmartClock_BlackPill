/*
 * ds3231.h
 *
 *  Created on: Aug 25, 2026
 *      Author: ivan
 */

#ifndef SRC_COMPONENTS_DS3231_DS3231_H_
#define SRC_COMPONENTS_DS3231_DS3231_H_

#include "main.h"

typedef struct {
	uint8_t hours;
	uint8_t minutes;
	uint8_t seconds;
} Time;

typedef struct {
	uint8_t day;
	uint8_t month;
	uint8_t year;
} Date;

HAL_StatusTypeDef rtc_read(Time *t, Date *d);
HAL_StatusTypeDef rtc_set_time(Time t, Date d);

#endif /* SRC_COMPONENTS_DS3231_DS3231_H_ */
