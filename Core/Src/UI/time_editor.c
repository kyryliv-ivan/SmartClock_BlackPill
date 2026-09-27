#include "time_editor.h"
#include "oled.h"
#include "alarm.h"
#include <stdio.h>
#include <string.h>

static int32_t wrap(int32_t v, int32_t min, int32_t max)
{
    int32_t span = max - min + 1;
    return min + ((v - min) % span + span) % span;
}

static void seed_from_rtc(TimeEditor_t *ed)
{
    ed->hours   = hour_get();
    ed->minutes = minute_get();
    ed->day     = day_get();
    ed->month   = month_get();
    ed->year    = year_get();
}

void time_editor_start_time(TimeEditor_t *ed)
{
    seed_from_rtc(ed);
    ed->field = EDIT_HOUR;
    ed->mode  = EDIT_MODE_TIME;
}

void time_editor_start_date(TimeEditor_t *ed)
{
    seed_from_rtc(ed);
    ed->field = EDIT_DAY;
    ed->mode  = EDIT_MODE_DATE;
}

void time_editor_start_alarm(TimeEditor_t *ed)
{
    ed->hours   = alarm_hour_get();
    ed->minutes = alarm_minute_get();
    ed->field   = EDIT_HOUR;
    ed->mode    = EDIT_MODE_ALARM;
}

void time_editor_rotate(TimeEditor_t *ed, int32_t delta)
{
    switch (ed->field)
    {
        case EDIT_HOUR:   ed->hours   = (uint8_t)wrap(ed->hours   + delta, 0, 23); break;
        case EDIT_MINUTE: ed->minutes = (uint8_t)wrap(ed->minutes + delta, 0, 59); break;
        case EDIT_DAY:    ed->day     = (uint8_t)wrap(ed->day     + delta, 1, 31); break;
        case EDIT_MONTH:  ed->month   = (uint8_t)wrap(ed->month   + delta, 1, 12); break;
        case EDIT_YEAR:   ed->year    = (uint8_t)wrap(ed->year    + delta, 0, 99); break;
    }
}

uint8_t time_editor_tap(TimeEditor_t *ed)
{
    if (ed->mode == EDIT_MODE_TIME || ed->mode == EDIT_MODE_ALARM)
    {
        if (ed->field == EDIT_MINUTE) return 1;
        ed->field = EDIT_MINUTE;
        return 0;
    }
    else
    {
        if (ed->field == EDIT_YEAR) return 1;
        ed->field = (ed->field == EDIT_DAY) ? EDIT_MONTH : EDIT_YEAR;
        return 0;
    }
}

void time_editor_commit(const TimeEditor_t *ed)
{
    if (ed->mode == EDIT_MODE_ALARM)
        alarm_set_time(ed->hours, ed->minutes);
    else
        time_set(ed->hours, ed->minutes, ed->day, ed->month, ed->year);
}

void time_editor_draw(const TimeEditor_t *ed)
{
    char line[20];
    char cursor[20];
    memset(cursor, ' ', sizeof(cursor));

    oled_clear();

    if (ed->mode == EDIT_MODE_TIME || ed->mode == EDIT_MODE_ALARM)
    {
        sprintf(line, "%02d:%02d", ed->hours, ed->minutes);
        uint8_t col = (ed->field == EDIT_HOUR) ? 0 : 3;
        cursor[col] = '^'; cursor[col + 1] = '^'; cursor[col + 2] = '\0';
        oled_line_small(0, 0, ed->mode == EDIT_MODE_ALARM ? "Set alarm:" : "Set time:");
    }
    else
    {
        sprintf(line, "%02d.%02d.%02d", ed->day, ed->month, ed->year);
        uint8_t col = (ed->field == EDIT_DAY) ? 0 : (ed->field == EDIT_MONTH) ? 3 : 6;
        cursor[col] = '^'; cursor[col + 1] = '^'; cursor[col + 2] = '\0';
        oled_line_small(0, 0, "Set date:");
    }

    oled_line_large(0, 16, line);
    oled_line_large(0, 36, cursor);
    oled_flush();
}
