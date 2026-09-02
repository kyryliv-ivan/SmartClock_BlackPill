#include "alarm.h"

static uint8_t enabled     = 0;
static uint8_t alarm_hour  = 7;
static uint8_t alarm_min   = 0;
static uint8_t ringing     = 0;
static uint8_t fired_today = 0;   /* guards against re-firing every tick within the same minute */

static const char *submenu_labels_[ALARM_SUBMENU_COUNT] = { "On/Off", "Set Time" };

void alarm_set_enabled(uint8_t on)
{
	enabled = on;
	if (!on) ringing = 0;
}

uint8_t alarm_enabled_get(void) { return enabled; }

void alarm_set_time(uint8_t hour, uint8_t minute)
{
	alarm_hour = hour;
	alarm_min  = minute;
}

uint8_t alarm_hour_get(void)   { return alarm_hour; }
uint8_t alarm_minute_get(void) { return alarm_min; }

uint8_t alarm_check(uint8_t hour, uint8_t minute)
{
	if (!enabled) return 0;

	if (hour == alarm_hour && minute == alarm_min)
	{
		if (fired_today) return 0;
		fired_today = 1;
		ringing = 1;
		return 1;
	}

	fired_today = 0;
	return 0;
}

uint8_t alarm_is_ringing(void) { return ringing; }
void    alarm_stop(void)       { ringing = 0; }

uint8_t alarm_submenu_count(void) { return ALARM_SUBMENU_COUNT; }

const char *alarm_submenu_label(uint8_t index)
{
	return submenu_labels_[index];
}

alarm_action_t alarm_submenu_tap(uint8_t index)
{
	if (index == 0)
	{
		alarm_set_enabled(!enabled);
		return ALARM_ACTION_NONE;
	}
	return ALARM_ACTION_EDIT_TIME;
}
