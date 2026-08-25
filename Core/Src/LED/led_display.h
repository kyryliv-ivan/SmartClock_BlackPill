/*
 * led_display.h
 *
 *  Created on: Aug 25, 2026
 *      Author: ivan
 */

#ifndef SRC_LED_LED_DISPLAY_H_
#define SRC_LED_LED_DISPLAY_H_


#include "main.h"

void led_display_init(void);
void led_display_set_time(uint8_t hours, uint8_t minutes);
void led_display_set_date(uint8_t day, uint8_t month, uint16_t year);
void led_display_set_colon(uint8_t on);
void led_display_set_error(void);


#endif /* SRC_LED_LED_DISPLAY_H_ */
