#ifndef LED_MENU_H
#define LED_MENU_H

#include "main.h"

void led_menu_init(void);

/* call every pass (or on the existing periodic tick) - advances the
   rotation timer and pushes the currently active reading to the LED
   display */
void led_menu_tick(void);

/* "LED Display" screen - multi-select checklist of what to show */
void led_select_rotate(int32_t delta);
void led_select_toggle(void);
void led_select_draw(void);

/* "LED Interval" screen - single-select seconds-per-item */
void led_interval_rotate(int32_t delta);
void led_interval_draw(void);

#endif /* LED_MENU_H */
