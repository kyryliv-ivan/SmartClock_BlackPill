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

#define ALARM_SUBMENU_COUNT 2

uint8_t     alarm_submenu_count(void);
const char *alarm_submenu_label(uint8_t index);

typedef enum { ALARM_ACTION_NONE, ALARM_ACTION_EDIT_TIME } alarm_action_t;
alarm_action_t alarm_submenu_tap(uint8_t index);

#endif /* ALARM_H */
