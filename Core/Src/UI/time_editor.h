#ifndef TIME_EDITOR_H
#define TIME_EDITOR_H

#include "sensors.h"

typedef enum { EDIT_HOUR, EDIT_MINUTE, EDIT_DAY, EDIT_MONTH, EDIT_YEAR } edit_field_t;
typedef enum { EDIT_MODE_TIME, EDIT_MODE_DATE, EDIT_MODE_ALARM } edit_mode_t;

typedef struct
{
    uint8_t      hours, minutes, day, month, year;
    edit_field_t field;
    edit_mode_t  mode;
} TimeEditor_t;

void time_editor_start_time(TimeEditor_t *ed);   /* edit Hour/Minute only */
void time_editor_start_date(TimeEditor_t *ed);   /* edit Day/Month/Year only */
void time_editor_start_alarm(TimeEditor_t *ed);  /* edit Hour/Minute, commits to the alarm */

void    time_editor_rotate(TimeEditor_t *ed, int32_t delta);
uint8_t time_editor_tap(TimeEditor_t *ed);       /* 1 = last field just confirmed */
void    time_editor_commit(const TimeEditor_t *ed);
void    time_editor_draw(const TimeEditor_t *ed);

#endif /* TIME_EDITOR_H */
