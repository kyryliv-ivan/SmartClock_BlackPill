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
void led_display_set_temp(int8_t temp_c);
void led_display_set_humidity(uint8_t rh_percent);
void led_display_set_pressure(uint16_t hpa);
void led_display_set_eq(uint8_t l0, uint8_t l1, uint8_t l2, uint8_t l3);

#define LED_BRIGHTNESS_MAX 10

void    led_brightness_set(uint8_t level);      /* clamped to 0..LED_BRIGHTNESS_MAX */
uint8_t led_brightness_get(void);
void    led_brightness_adjust(int32_t delta);   /* clamped +/- step */

#endif /* SRC_LED_LED_DISPLAY_H_ */
