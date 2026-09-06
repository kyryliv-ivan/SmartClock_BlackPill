#include "menu.h"
#include "oled.h"
#include "time_editor.h"
#include "alarm.h"
#include "radio.h"
#include "wifi.h"
#include "settings.h"
#include "stopwatch.h"
#include "led_menu.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

typedef enum {
	UI_CLOCK, UI_MENU, UI_SUBMENU, UI_EDIT, UI_STOPWATCH,
	UI_LED_SELECT, UI_LED_INTERVAL, UI_VOLUME, UI_WIFI_QR
} ui_mode_t;

typedef enum {
	MENU_ALARM, MENU_RADIO, MENU_WIFI, MENU_SETTINGS, MENU_STOPWATCH, MENU_COUNT
} menu_item_t;

static const char *menu_labels[MENU_COUNT] =
	{ "Alarm", "Radio", "WiFi", "Settings", "Stopwatch" };

/* rows visible at once in the top-level menu at Font_11x18's 20px pitch
   on a 64px-tall display (0, 20, 44 -> last row ends at 62) */
#define MENU_VISIBLE_ROWS 3

/* rows visible at once in a submenu list at Font_7x10's 12px pitch, under
   the header row (14, 26, 38, 50 -> last row ends at 60) */
#define SUBMENU_VISIBLE_ROWS 4

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

/* Safety net: Forget WiFi is ignored for a few seconds right after a
   successful (re)connect, so a stray/duplicate tap - or WiFi-radio EMI
   glitching the encoder's SW line during the AP shutdown/STA connect
   transition - can't immediately erase the WiFi credentials it just took
   the setup flow to save, looping connect -> forget -> setup forever. */
static uint32_t forget_wifi_cooldown_until = 0;

/* Guards the auto-enter-UI_WIFI_QR-from-clock path in menu_tick(): without
   this, wifi_setup_mode_get() just stays true for as long as ESP32's AP is
   up (i.e. until setup is actually completed on the phone), so the instant
   the QR screen is dismissed - by tap or by its own timeout - the very next
   tick would see UI_CLOCK + setup-mode-still-true and re-enter it right
   away, permanently locking the display on the QR. Shown once per setup
   session; only resets once setup mode actually ends. */
static uint8_t qr_shown_this_setup = 0;

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

	if (ui_mode == UI_CLOCK)
	{
		/* quick access - rotating with nothing open adjusts radio volume
		   directly, no need to dive into Settings first */
		radio_volume_adjust(delta);
		ui_mode = UI_VOLUME;
	}
	else if (ui_mode == UI_MENU)
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
	else if (ui_mode == UI_VOLUME)
	{
		radio_volume_adjust(delta);
	}
	else
	{
		return; /* UI_STOPWATCH - rotation does nothing */
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
			case SETTINGS_ACTION_LED_INTERVAL:
				ui_mode = UI_LED_INTERVAL;
				break;
			default:
				ui_mode = UI_VOLUME;
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
		{
			wifi_action_t action = wifi_submenu_tap((uint8_t) submenu_index);

			if (action == WIFI_ACTION_FORGET && HAL_GetTick() < forget_wifi_cooldown_until)
			{
				/* ignored - see forget_wifi_cooldown_until comment */
			}
			else if (action == WIFI_ACTION_FORGET)
			{
				const char *cmd = "FORGET_WIFI:1\n";
				HAL_UART_Transmit(&huart1, (uint8_t*) cmd,
						(uint16_t) strlen(cmd), 100);

				/* Land back on "Status" next time this submenu opens, not
				   "Forget WiFi" - the cursor otherwise stays right on the
				   destructive entry, one stray tap away from firing again. */
				last_submenu_index[MENU_WIFI] = 0;

				/* Show the QR right away - ESP32 takes ~10s to actually
				   confirm setup mode (SETUP_MODE:1), during which
				   wifi_setup_mode_get() is still false. menu_tick() below
				   dismisses this screen once it's seen setup mode go true
				   then false again (i.e. genuinely reconnected), or a
				   tap/timeout dismisses it same as any other screen. */
				ui_mode = UI_WIFI_QR;
				menu_needs_redraw = 1;
				qr_shown_this_setup = 1;
			}
			else if (submenu_index == 0) /* Status */
			{
				oled_clear();
				oled_line_small(0, 0, "WiFi Status");
				oled_line_large(0, 20,
						wifi_connected_get() ? "Connected" : "No WiFi");
				if (wifi_connected_get())
					oled_line_small(0, 46, wifi_ssid_get());
				oled_flush();
				HAL_Delay(1200);
				return_to_clock();
			}
			else /* Reconnect */
			{
				const char *cmd = "RECONNECT:1\n";
				HAL_UART_Transmit(&huart1, (uint8_t*) cmd,
						(uint16_t) strlen(cmd), 100);

				oled_clear();
				oled_line_large(0, 24, "Reconnecting");
				oled_flush();
				HAL_Delay(600);
				return_to_clock();
			}
			break;
		}
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
	else if (ui_mode == UI_VOLUME)
	{
		return_to_clock();
	}
	else if (ui_mode == UI_WIFI_QR)
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

	/* wifi_setup_mode_get() reads false for ~10s right after a Forget WiFi
	   tap too (ESP32 hasn't confirmed SETUP_MODE:1 yet) - resetting the
	   latch on every tick it happens to be false would wipe out the tap
	   handler's qr_shown_this_setup=1 during that gap. Only reset on the
	   actual falling edge (was in setup mode, now genuinely isn't - i.e.
	   connected), not on every "currently false" tick. */
	static uint8_t prev_setup_mode = 0;
	uint8_t setup_mode_now = wifi_setup_mode_get();

	if (prev_setup_mode && !setup_mode_now)
	{
		qr_shown_this_setup = 0;
	}
	prev_setup_mode = setup_mode_now;

	/* ESP32 gives up retrying a broken saved connection on its own
	   (STA_RETRY_LIMIT) and raises the setup AP without any tap here - show
	   the QR for that path too, not just the explicit Forget WiFi tap, but
	   only from the idle clock screen (so it never interrupts someone
	   actually navigating the menu) and only once per setup session (so
	   dismissing it - by tap or by timeout - doesn't just bring it right
	   back on the next tick). */
	if (setup_mode_now && ui_mode == UI_CLOCK && !qr_shown_this_setup)
	{
		ui_mode = UI_WIFI_QR;
		menu_needs_redraw = 1;
		menu_last_activity = HAL_GetTick();
		qr_shown_this_setup = 1;
	}

	/* Arm the Forget WiFi cooldown on ANY genuine connect, not just one that
	   happens to occur while the QR screen is still up - the QR can easily
	   be gone by then (tap, or its own 120s timeout) well before the phone
	   flow actually finishes, and a reconnect right after that previously
	   left the cooldown disarmed entirely, letting a stray/phantom trigger
	   (e.g. EMI on the encoder's SW line during the high-current STA
	   connect event) erase credentials seconds after they were saved. */
	static uint8_t prev_wifi_connected = 0;
	uint8_t wifi_connected_now = wifi_connected_get();

	if (!prev_wifi_connected && wifi_connected_now)
	{
		forget_wifi_cooldown_until = HAL_GetTick() + 5000;
	}
	prev_wifi_connected = wifi_connected_now;

	/* wifi_setup_mode_get() only flips true once ESP32 confirms it's up in
	   AP mode (SETUP_MODE:1), which takes ~10s after a Forget WiFi tap -
	   far shorter than that would make this screen dismiss itself before
	   the flag ever gets a chance to go true. So: remember once we've seen
	   it true while this screen is showing, and only treat a later false as
	   "genuinely reconnected" (not "hasn't started yet"). */
	static uint8_t seen_setup_active = 0;

	if (ui_mode != UI_WIFI_QR)
	{
		seen_setup_active = 0;
	}
	else if (wifi_setup_mode_get())
	{
		seen_setup_active = 1;
	}
	else if (seen_setup_active)
	{
		seen_setup_active = 0;
		forget_wifi_cooldown_until = HAL_GetTick() + 5000;
		return_to_clock();
	}

	/* The full setup flow (connect phone to the AP, open the browser, pick
	   a network, type its password, submit) realistically takes way
	   longer than a normal menu screen. */
	uint32_t timeout_ms = (ui_mode == UI_WIFI_QR) ? 120000 : 6000;

	if (ui_mode != UI_CLOCK && ui_mode != UI_STOPWATCH
			&& HAL_GetTick() - menu_last_activity >= timeout_ms)
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

		/* same scrolling-window trick as the top-level menu - only
		   SUBMENU_VISIBLE_ROWS fit under the header at Font_7x10's 12px
		   pitch, so keep the window centred on submenu_index instead of
		   only ever showing the first few entries */
		uint8_t window_start = 0;
		if (submenu_index >= SUBMENU_VISIBLE_ROWS)
			window_start = submenu_index - SUBMENU_VISIBLE_ROWS + 1;
		if (count > SUBMENU_VISIBLE_ROWS
				&& window_start > count - SUBMENU_VISIBLE_ROWS)
			window_start = count - SUBMENU_VISIBLE_ROWS;

		oled_clear();
		oled_line_small(0, 0, menu_labels[menu_index]);
		for (uint8_t row = 0;
				row < SUBMENU_VISIBLE_ROWS && (window_start + row) < count;
				row++)
		{
			uint8_t i = window_start + row;
			oled_line_small(0, row * 12 + 14, i == submenu_index ? ">" : " ");
			oled_line_small(10, row * 12 + 14, current_submenu_label(i));
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
	else if (ui_mode == UI_VOLUME)
	{
		uint8_t vol = radio_volume_get();

		char bar[RADIO_VOLUME_MAX + 1];
		for (uint8_t i = 0; i < RADIO_VOLUME_MAX; i++)
			bar[i] = (i < vol) ? '#' : '-';
		bar[RADIO_VOLUME_MAX] = '\0';

		char line[16];
		sprintf(line, "Vol: %u/%u", vol, RADIO_VOLUME_MAX);

		oled_clear();
		oled_line_small(0, 0, "Volume");
		oled_line_large(0, 16, line);
		oled_line_small(0, 44, bar);
		oled_flush();
	}
	else if (ui_mode == UI_WIFI_QR)
	{
		wifi_qr_draw();
	}
}
