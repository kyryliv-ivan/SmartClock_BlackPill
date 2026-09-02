#include "radio.h"
#include "usart.h"
#include <stdio.h>

/* Radio station names - must stay in the same order as `stations[]` on the
   ESP32 side (see ESP32/SmartClock_ESP32/SmartClock_ESP32.ino), since only
   the index is sent over UART, never the name. */
static const char *labels[] = { "Hit FM", "Radio ROKS", "Kiss FM" };
#define COUNT (sizeof(labels) / sizeof(labels[0]))

uint8_t radio_submenu_count(void) { return COUNT; }

const char *radio_submenu_label(uint8_t index)
{
	return labels[index];
}

void radio_submenu_tap(uint8_t index)
{
	char line[16];
	int len = snprintf(line, sizeof(line), "STATION:%u\n", (unsigned) index);

	/* rare, user-triggered event - a short blocking send is fine here */
	HAL_UART_Transmit(&huart1, (uint8_t*) line, (uint16_t) len, 100);
}
