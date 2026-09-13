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

#define LED_BRIGHTNESS_MAX 15

void    led_brightness_set(uint8_t level);      /* clamped to 0..LED_BRIGHTNESS_MAX */
uint8_t led_brightness_get(void);
void    led_brightness_adjust(int32_t delta);   /* clamped +/- step */

/* Night Mode: while on, brightness is driven automatically from the
   BH1750's lux reading instead of the manual dial above (led_brightness_set
   still remembers the dial's position, it just isn't applied to the
   display until Night Mode is turned back off). */
void    led_night_mode_set(uint8_t on);
uint8_t led_night_mode_get(void);

/* Call periodically (e.g. once per main-loop tick) with the current lux
   reading - no-op unless Night Mode is on. Internally smoothed and
   hysteresis-banded, so it's fine to call this often with a raw, noisy
   reading. */
void    led_night_mode_apply(float lux);

#endif /* SRC_LED_LED_DISPLAY_H_ */
