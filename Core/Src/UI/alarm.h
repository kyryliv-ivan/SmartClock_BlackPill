#ifndef ALARM_H
#define ALARM_H

#include "main.h"

void    alarm_set_enabled(uint8_t on);
uint8_t alarm_enabled_get(void);

void    alarm_set_time(uint8_t hour, uint8_t minute);
uint8_t alarm_hour_get(void);
uint8_t alarm_minute_get(void);

/* call every pass with the current time - returns 1 once, the instant the
   alarm fires (won't re-fire again until the minute has passed) */
uint8_t alarm_check(uint8_t hour, uint8_t minute);

uint8_t alarm_is_ringing(void);
void    alarm_stop(void);

#define ALARM_SUBMENU_COUNT 4
#define ALARM_MELODY_COUNT  5
#define ALARM_VOLUME_MAX    10

uint8_t     alarm_submenu_count(void);
const char *alarm_submenu_label(uint8_t index);

typedef enum {
	ALARM_ACTION_NONE, ALARM_ACTION_EDIT_TIME, ALARM_ACTION_MELODY_LIST,
	ALARM_ACTION_VOLUME
} alarm_action_t;
alarm_action_t alarm_submenu_tap(uint8_t index);

/* separate from the radio's Volume (radio.c) - the alarm forces this level
   on ESP32 for the duration of the ring/preview, then restores whatever
   the radio was at, so a quiet radio setting never means a quiet alarm */
void    alarm_volume_set(uint8_t vol);      /* clamped to 0..ALARM_VOLUME_MAX */
uint8_t alarm_volume_get(void);
void    alarm_volume_adjust(int32_t delta); /* clamped +/- step */

/* which of the 6 synthesized melodies (see the ESP32 sketch) plays when the
   alarm fires - index sent verbatim over UART as ALARM_SOUND:<index> */
void        alarm_set_melody(uint8_t index);
uint8_t     alarm_melody_get(void);
const char *alarm_melody_name(uint8_t index);

/* scrollable single-select list (UI_ALARM_MELODY in menu.c) - rotate moves
   the highlighted row AND commits it immediately (same value, no separate
   confirm step), tap just leaves; a checkmark marks the active row. */
void alarm_melody_rotate(int32_t delta);
void alarm_melody_draw(void);

#endif /* ALARM_H */
