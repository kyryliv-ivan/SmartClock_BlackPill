#include "led_menu.h"
#include "sensors.h"
#include "led_display.h"
#include "oled.h"
#include <stdio.h>

typedef enum {
	LED_MODE_TIME, LED_MODE_SECONDS, LED_MODE_DATE,
	LED_MODE_TEMP, LED_MODE_HUMIDITY, LED_MODE_PRESSURE, LED_MODE_COUNT
} led_mode_t;

static const char *mode_labels[LED_MODE_COUNT] =
	{ "Time", "Seconds", "Date", "Temp", "Humidity", "Pressure" };

static const uint8_t interval_options[] =
	{ 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60 };
#define INTERVAL_COUNT (sizeof(interval_options) / sizeof(interval_options[0]))

/* Font_7x10 @ 10px pitch, under a header row, on a 64px screen */
#define LIST_VISIBLE_ROWS 5

static led_mode_t cursor = LED_MODE_TIME;
static uint8_t mode_enabled[LED_MODE_COUNT] = { 0 };

static uint8_t interval_index = 0;   /* 5s default */
static led_mode_t active_mode = LED_MODE_TIME;
static uint32_t cycle_tick = 0;

void led_menu_init(void)
{
	mode_enabled[LED_MODE_TIME] = 1;
}

void led_menu_tick(void)
{
	uint32_t interval_ms = (uint32_t) interval_options[interval_index] * 1000;

	if (HAL_GetTick() - cycle_tick >= interval_ms)
	{
		cycle_tick = HAL_GetTick();

		for (uint8_t i = 1; i <= LED_MODE_COUNT; i++)
		{
			led_mode_t candidate = (led_mode_t) ((active_mode + i) % LED_MODE_COUNT);
			if (mode_enabled[candidate])
			{
				active_mode = candidate;
				break;
			}
		}
		/* nothing enabled - loop finds no candidate, active_mode just stays put */
	}

	switch (active_mode)
	{
	case LED_MODE_TIME:
		led_display_set_time(hour_get(), minute_get());
		led_display_set_colon(1);
		break;
	case LED_MODE_SECONDS:
		led_display_set_time(minute_get(), second_get());
		led_display_set_colon(1);
		break;
	case LED_MODE_DATE:
		led_display_set_time(day_get(), month_get());
		led_display_set_colon(0);
		break;
	case LED_MODE_TEMP:
		led_display_set_temp((int8_t) temperature_get());
		break;
	case LED_MODE_HUMIDITY:
		led_display_set_humidity((uint8_t) humidity_get());
		break;
	case LED_MODE_PRESSURE:
		led_display_set_pressure((uint16_t) pressure_get());
		break;
	default:
		break;
	}
}

void led_select_rotate(int32_t delta)
{
	int32_t idx = ((cursor + delta) % LED_MODE_COUNT + LED_MODE_COUNT)
			% LED_MODE_COUNT;
	cursor = (led_mode_t) idx;
}

void led_select_toggle(void)
{
	mode_enabled[cursor] = !mode_enabled[cursor];

	/* never allow every mode to end up off - fall back to Time so the LED
	   display always shows something */
	uint8_t any_enabled = 0;
	for (uint8_t i = 0; i < LED_MODE_COUNT; i++)
	{
		if (mode_enabled[i])
		{
			any_enabled = 1;
			break;
		}
	}

	if (!any_enabled)
		mode_enabled[LED_MODE_TIME] = 1;
}

void led_select_draw(void)
{
	uint8_t window_start = 0;
	if (cursor >= LIST_VISIBLE_ROWS)
		window_start = cursor - LIST_VISIBLE_ROWS + 1;
	if (LED_MODE_COUNT > LIST_VISIBLE_ROWS
			&& window_start > LED_MODE_COUNT - LIST_VISIBLE_ROWS)
		window_start = LED_MODE_COUNT - LIST_VISIBLE_ROWS;

	oled_clear();
	oled_line_small(0, 0, "LED shows:");
	for (uint8_t row = 0;
			row < LIST_VISIBLE_ROWS && (window_start + row) < LED_MODE_COUNT;
			row++)
	{
		uint8_t i = window_start + row;
		oled_line_small(0, row * 10 + 14, i == cursor ? ">" : " ");
		oled_line_small(10, row * 10 + 14, mode_enabled[i] ? "[x]" : "[ ]");
		oled_line_small(34, row * 10 + 14, mode_labels[i]);
	}
	oled_flush();
}

void led_interval_rotate(int32_t delta)
{
	int32_t idx = ((interval_index + delta) % INTERVAL_COUNT + INTERVAL_COUNT)
			% INTERVAL_COUNT;
	interval_index = (uint8_t) idx;
}

void led_interval_draw(void)
{
	uint8_t window_start = 0;
	if (interval_index >= LIST_VISIBLE_ROWS)
		window_start = interval_index - LIST_VISIBLE_ROWS + 1;
	if (INTERVAL_COUNT > LIST_VISIBLE_ROWS
			&& window_start > INTERVAL_COUNT - LIST_VISIBLE_ROWS)
		window_start = INTERVAL_COUNT - LIST_VISIBLE_ROWS;

	oled_clear();
	oled_line_small(0, 0, "LED interval:");
	for (uint8_t row = 0;
			row < LIST_VISIBLE_ROWS && (window_start + row) < INTERVAL_COUNT;
			row++)
	{
		uint8_t i = window_start + row;
		char label[8];
		sprintf(label, "%u s", interval_options[i]);
		oled_line_small(0, row * 10 + 14, i == interval_index ? ">" : " ");
		oled_line_small(10, row * 10 + 14, label);
	}
	oled_flush();
}
