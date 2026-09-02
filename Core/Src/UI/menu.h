#ifndef MENU_H
#define MENU_H

#include "main.h"

void menu_init(void);

void menu_rotate(int32_t delta);
void menu_tap(void);

/* call every main-loop pass - inactivity timeout + self-driven redraws
   (e.g. a running stopwatch) */
void menu_tick(void);

/* draws whatever the menu currently needs (no-op if nothing changed) */
void menu_draw(void);

/* true while the menu owns the OLED (anything but the plain clock view) */
uint8_t menu_active(void);

/* 1 once, right when the menu hands the screen back to the clock - lets
   main.c force an immediate clock redraw instead of waiting for the
   normal string-compare tick */
uint8_t menu_consume_clock_redraw(void);

#endif /* MENU_H */
