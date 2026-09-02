#include "menu.h"
#include "oled.h"
#include "time_editor.h"
#include "alarm.h"
#include "radio.h"
#include "wifi.h"
#include "settings.h"
#include "stopwatch.h"
#include "led_menu.h"
#include <stdio.h>

typedef enum {
	UI_CLOCK, UI_MENU, UI_SUBMENU, UI_EDIT, UI_STOPWATCH,
	UI_LED_SELECT, UI_LED_INTERVAL
} ui_mode_t;

typedef enum {
	MENU_ALARM, MENU_RADIO, MENU_WIFI, MENU_SETTINGS, MENU_STOPWATCH, MENU_COUNT
} menu_item_t;

static const char *menu_labels[MENU_COUNT] =
	{ "Alarm", "Radio", "WiFi", "Settings", "Stopwatch" };

/* rows visible at once in the top-level menu at Font_11x18's 20px pitch
   on a 64px-tall display (0, 20, 44 -> last row ends at 62) */
#define MENU_VISIBLE_ROWS 3

static ui_mode_t ui_mode = UI_CLOCK;
static TimeEditor_t time_ed;

static int8_t   menu_index = 0;
static int8_t   submenu_index = 0;
static uint32_t menu_last_activity = 0;
static uint8_t  menu_needs_redraw = 0;

/* remembers the last entry picked in each submenu (e.g. which radio
   station is currently playing), so reopening a submenu highlights the
   current choice instead of always starting back at index 0 */
static int8_t last_submenu_index[MENU_COUNT] = { 0 };

static uint8_t clock_redraw_pending = 0;

static uint8_t current_submenu_count(void)
{
	switch ((menu_item_t) menu_index)
	{
	case MENU_ALARM:    return alarm_submenu_count();
	case MENU_RADIO:    return radio_submenu_count();
	case MENU_WIFI:     return wifi_submenu_count();
	case MENU_SETTINGS: return settings_submenu_count();
	default: return 0;
	}
}

static const char *current_submenu_label(uint8_t index)
{
	switch ((menu_item_t) menu_index)
	{
	case MENU_ALARM:    return alarm_submenu_label(index);
	case MENU_RADIO:    return radio_submenu_label(index);
	case MENU_WIFI:     return wifi_submenu_label(index);
	case MENU_SETTINGS: return settings_submenu_label(index);
	default: return "";
	}
}

static void return_to_clock(void)
{
	ui_mode = UI_CLOCK;
	clock_redraw_pending = 1;
}

void menu_init(void)
{
}

uint8_t menu_active(void)
{
	return ui_mode != UI_CLOCK;
}

uint8_t menu_consume_clock_redraw(void)
{
	if (clock_redraw_pending)
	{
		clock_redraw_pending = 0;
		return 1;
	}
	return 0;
}

void menu_rotate(int32_t delta)
{
	if (delta == 0) return;

	if (ui_mode == UI_MENU)
	{
		int32_t idx = ((menu_index + delta) % MENU_COUNT + MENU_COUNT) % MENU_COUNT;
		menu_index = (int8_t) idx;
	}
	else if (ui_mode == UI_SUBMENU)
	{
		uint8_t count = current_submenu_count();
		int32_t idx = ((submenu_index + delta) % count + count) % count;
		submenu_index = (int8_t) idx;
	}
	else if (ui_mode == UI_EDIT)
	{
		time_editor_rotate(&time_ed, delta);
	}
	else if (ui_mode == UI_LED_SELECT)
	{
		led_select_rotate(delta);
	}
	else if (ui_mode == UI_LED_INTERVAL)
	{
		led_interval_rotate(delta);
	}
	else
	{
		return; /* UI_CLOCK / UI_STOPWATCH - rotation does nothing */
	}

	menu_needs_redraw = 1;
	menu_last_activity = HAL_GetTick();
}

void menu_tap(void)
{
	menu_last_activity = HAL_GetTick();

	if (ui_mode == UI_CLOCK)
	{
		ui_mode = UI_MENU;
		menu_index = 0;
		menu_needs_redraw = 1;
	}
	else if (ui_mode == UI_MENU)
	{
		if ((menu_item_t) menu_index == MENU_STOPWATCH)
		{
			ui_mode = UI_STOPWATCH;
		}
		else
		{
			ui_mode = UI_SUBMENU;
			submenu_index = last_submenu_index[menu_index];
		}
		menu_needs_redraw = 1;
	}
	else if (ui_mode == UI_SUBMENU)
	{
		last_submenu_index[menu_index] = submenu_index;

		switch ((menu_item_t) menu_index)
		{
		case MENU_ALARM:
		{
			alarm_action_t action = alarm_submenu_tap((uint8_t) submenu_index);
			if (action == ALARM_ACTION_EDIT_TIME)
			{
				time_editor_start_alarm(&time_ed);
				ui_mode = UI_EDIT;
				menu_needs_redraw = 1;
			}
			else
			{
				oled_clear();
				oled_line_large(0, 24, alarm_enabled_get() ? "Alarm ON" : "Alarm OFF");
				oled_flush();
				HAL_Delay(600);
				return_to_clock();
			}
			break;
		}
		case MENU_SETTINGS:
		{
			settings_action_t action = settings_submenu_tap((uint8_t) submenu_index);
			switch (action)
			{
			case SETTINGS_ACTION_EDIT_TIME:
				time_editor_start_time(&time_ed);
				ui_mode = UI_EDIT;
				break;
			case SETTINGS_ACTION_EDIT_DATE:
				time_editor_start_date(&time_ed);
				ui_mode = UI_EDIT;
				break;
			case SETTINGS_ACTION_LED_SELECT:
				ui_mode = UI_LED_SELECT;
				break;
			default:
				ui_mode = UI_LED_INTERVAL;
				break;
			}
			menu_needs_redraw = 1;
			break;
		}
		case MENU_RADIO:
			radio_submenu_tap((uint8_t) submenu_index);
			oled_clear();
			oled_line_large(0, 24, radio_submenu_label((uint8_t) submenu_index));
			oled_line_small(0, 48, "selected");
			oled_flush();
			HAL_Delay(600);
			return_to_clock();
			break;
		case MENU_WIFI:
			oled_clear();
			oled_line_large(0, 24, wifi_submenu_label((uint8_t) submenu_index));
			oled_line_small(0, 48, "selected");
			oled_flush();
			HAL_Delay(600);
			return_to_clock();
			break;
		default:
			break;
		}
	}
	else if (ui_mode == UI_EDIT)
	{
		if (time_editor_tap(&time_ed))
		{
			time_editor_commit(&time_ed);
			return_to_clock();
		}
		menu_needs_redraw = 1;
	}
	else if (ui_mode == UI_STOPWATCH)
	{
		if (!stopwatch_running_get() && stopwatch_elapsed_ms_get() > 0)
		{
			stopwatch_reset();
			return_to_clock();
		}
		else
		{
			stopwatch_start_stop();
		}
		menu_needs_redraw = 1;
	}
	else if (ui_mode == UI_LED_SELECT)
	{
		led_select_toggle();
		menu_needs_redraw = 1;
	}
	else if (ui_mode == UI_LED_INTERVAL)
	{
		return_to_clock();
	}
}

void menu_tick(void)
{
	if (ui_mode == UI_STOPWATCH)
	{
		static uint32_t sw_redraw_tick = 0;
		if (stopwatch_running_get() && HAL_GetTick() - sw_redraw_tick >= 50)
		{
			sw_redraw_tick = HAL_GetTick();
			menu_needs_redraw = 1;
		}
	}

	if (ui_mode != UI_CLOCK && ui_mode != UI_STOPWATCH
			&& HAL_GetTick() - menu_last_activity >= 6000)
	{
		return_to_clock();
	}
}

void menu_draw(void)
{
	if (!menu_needs_redraw) return;
	menu_needs_redraw = 0;

	if (ui_mode == UI_MENU)
	{
		/* Only 3 rows fit on a 64px-tall display at Font_11x18's 20px pitch.
		   With more menu items than that, scroll a 3-row window so it always
		   contains menu_index - recomputed fresh each redraw from
		   menu_index alone, no extra persistent state needed. */
		uint8_t window_start = 0;
		if (menu_index >= MENU_VISIBLE_ROWS)
			window_start = menu_index - MENU_VISIBLE_ROWS + 1;
		if (MENU_COUNT > MENU_VISIBLE_ROWS
				&& window_start > MENU_COUNT - MENU_VISIBLE_ROWS)
			window_start = MENU_COUNT - MENU_VISIBLE_ROWS;

		oled_clear();
		for (uint8_t row = 0;
				row < MENU_VISIBLE_ROWS && (window_start + row) < MENU_COUNT; row++)
		{
			uint8_t i = window_start + row;
			oled_line_large(0, row * 20 + 4, i == menu_index ? ">" : " ");
			oled_line_large(16, row * 20 + 4, menu_labels[i]);
		}
		oled_flush();
	}
	else if (ui_mode == UI_SUBMENU)
	{
		uint8_t count = current_submenu_count();

		oled_clear();
		oled_line_small(0, 0, menu_labels[menu_index]);
		for (uint8_t i = 0; i < count && i < 4; i++)
		{
			oled_line_small(0, 14 + i * 12, i == submenu_index ? ">" : " ");
			oled_line_small(10, 14 + i * 12, current_submenu_label(i));
		}
		oled_flush();
	}
	else if (ui_mode == UI_EDIT)
	{
		time_editor_draw(&time_ed);
	}
	else if (ui_mode == UI_STOPWATCH)
	{
		uint32_t ms = stopwatch_elapsed_ms_get();
		uint32_t total_s = ms / 1000;
		uint8_t mm = (total_s / 60) % 60;
		uint8_t ss = total_s % 60;
		uint8_t cs = (ms % 1000) / 10;

		char buf[16];
		sprintf(buf, "%02d:%02d.%02d", mm, ss, cs);

		oled_clear();
		oled_line_small(0, 0, "Stopwatch:");
		oled_line_large(0, 20, buf);
		oled_line_small(0, 48, stopwatch_running_get() ? "tap: stop" : "tap: start/reset");
		oled_flush();
	}
	else if (ui_mode == UI_LED_SELECT)
	{
		led_select_draw();
	}
	else if (ui_mode == UI_LED_INTERVAL)
	{
		led_interval_draw();
	}
}
