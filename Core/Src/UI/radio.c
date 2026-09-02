#include "radio.h"
#include "usart.h"
#include <stdio.h>

/* Radio station names - must stay in the same order as `stations[]` on the
   ESP32 side (see ESP32/SmartClock_ESP32/SmartClock_ESP32.ino), since only
   the index is sent over UART, never the name. ASCII only - Font_7x10 has
   no Cyrillic glyphs. */
/* "Stop" is index 0 here but isn't a real station - stations[] on the
   ESP32 side stays unshifted, radio_submenu_tap() below maps index-1 onto
   it and reserves STATION:255 as a "stop playback" sentinel. */
static const char *labels[] = {
	"Stop",
	"Hit FM", "Radio ROKS", "Zakarpattya FM", "Kiss FM", "Radio Relax",
	"Melodia FM", "Nashe Radio", "Ukr Radio 1", "Ukr Radio 2", "Avtoradio",
	"Hromadske", "Ukr Radio 3", "Ukr Radio 4", "Kiss FM Ukr",
	"Kiss Digital", "ROKS Ukr", "ROKS New Rock", "Relax Instr",
	"Hit FM Ukr", "Hit FM Top", "Melodia Romantic", "Bayraktar",
	"Nakypilo", "MFM Ukraine", "Lviv Hvylya", "Radio Trek", "Lounge FM",
	"Jazz FM", "Radio Maximum", "Zahid FM"
};
#define COUNT (sizeof(labels) / sizeof(labels[0]))
#define STOP_SENTINEL 255

uint8_t radio_submenu_count(void) { return COUNT; }

const char *radio_submenu_label(uint8_t index)
{
	return labels[index];
}

void radio_submenu_tap(uint8_t index)
{
	char line[16];
	unsigned station = (index == 0) ? STOP_SENTINEL : (unsigned) (index - 1);
	int len = snprintf(line, sizeof(line), "STATION:%u\n", station);

	/* rare, user-triggered event - a short blocking send is fine here */
	HAL_UART_Transmit(&huart1, (uint8_t*) line, (uint16_t) len, 100);
}

static uint8_t volume = 5;

static void send_volume(void)
{
	char line[16];
	int len = snprintf(line, sizeof(line), "VOLUME:%u\n", (unsigned) volume);
	HAL_UART_Transmit(&huart1, (uint8_t*) line, (uint16_t) len, 100);
}

void radio_volume_set(uint8_t vol)
{
	volume = (vol > RADIO_VOLUME_MAX) ? RADIO_VOLUME_MAX : vol;
	send_volume();
}

uint8_t radio_volume_get(void) { return volume; }

void radio_volume_adjust(int32_t delta)
{
	int32_t v = (int32_t) volume + delta;
	if (v < 0) v = 0;
	if (v > RADIO_VOLUME_MAX) v = RADIO_VOLUME_MAX;
	volume = (uint8_t) v;
	send_volume();
}
