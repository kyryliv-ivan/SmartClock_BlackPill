#include "alarm.h"
#include "oled.h"
#include "usart.h"
#include <stdio.h>

static uint8_t enabled      = 0;
static uint8_t alarm_hour   = 7;
static uint8_t alarm_min    = 0;
static uint8_t ringing      = 0;
static uint8_t fired_today  = 0;   /* guards against re-firing every tick within the same minute */
static uint8_t alarm_volume = 5;

static const char *submenu_labels_[ALARM_SUBMENU_COUNT] =
	{ "On/Off", "Set Time", "Melody", "Volume" };

/* names kept short (<=8 chars) so the confirmation screen fits Font_11x18
   on a 128px-wide OLED without clipping */
static const char *melody_names_[ALARM_MELODY_COUNT] =
	{ "Song 1", "Song 2", "Song 3", "Song 4", "Song 5" };
static uint8_t melody = 0;

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
	if (index == 2)
	{
		return ALARM_ACTION_MELODY_LIST;
	}
	if (index == 3)
	{
		return ALARM_ACTION_VOLUME;
	}
	return ALARM_ACTION_EDIT_TIME;
}

void alarm_set_melody(uint8_t index) { melody = index % ALARM_MELODY_COUNT; }
uint8_t alarm_melody_get(void) { return melody; }

const char *alarm_melody_name(uint8_t index)
{
	return melody_names_[index % ALARM_MELODY_COUNT];
}

static void send_alarm_volume(void)
{
	char line[20];
	int len = snprintf(line, sizeof(line), "ALARM_VOLUME:%u\n",
			(unsigned) alarm_volume);
	HAL_UART_Transmit(&huart1, (uint8_t*) line, (uint16_t) len, 100);
}

void alarm_volume_set(uint8_t vol)
{
	alarm_volume = (vol > ALARM_VOLUME_MAX) ? ALARM_VOLUME_MAX : vol;
	send_alarm_volume();
}

uint8_t alarm_volume_get(void) { return alarm_volume; }

void alarm_volume_adjust(int32_t delta)
{
	int32_t v = (int32_t) alarm_volume + delta;
	if (v < 0) v = 0;
	if (v > ALARM_VOLUME_MAX) v = ALARM_VOLUME_MAX;
	alarm_volume_set((uint8_t) v);
}

void alarm_melody_rotate(int32_t delta)
{
	int32_t v = ((int32_t) melody + delta) % ALARM_MELODY_COUNT;
	if (v < 0) v += ALARM_MELODY_COUNT;
	melody = (uint8_t) v;
}

/* rows visible at Font_7x10's 10px pitch under a header row on a 64px
   screen - same layout convention as led_menu.c's checkbox lists */
#define MELODY_VISIBLE_ROWS 5

void alarm_melody_draw(void)
{
	uint8_t window_start = 0;
	if (melody >= MELODY_VISIBLE_ROWS)
		window_start = melody - MELODY_VISIBLE_ROWS + 1;
	if (ALARM_MELODY_COUNT > MELODY_VISIBLE_ROWS
			&& window_start > ALARM_MELODY_COUNT - MELODY_VISIBLE_ROWS)
		window_start = ALARM_MELODY_COUNT - MELODY_VISIBLE_ROWS;

	oled_clear();
	oled_line_small(0, 0, "Alarm Melody:");
	for (uint8_t row = 0;
			row < MELODY_VISIBLE_ROWS && (window_start + row) < ALARM_MELODY_COUNT;
			row++)
	{
		uint8_t i = window_start + row;
		oled_line_small(0, row * 10 + 14, i == melody ? ">" : " ");
		oled_line_small(10, row * 10 + 14, i == melody ? "[x]" : "[ ]");
		oled_line_small(34, row * 10 + 14, melody_names_[i]);
	}
	oled_flush();
}
