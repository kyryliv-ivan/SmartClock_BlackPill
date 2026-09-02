#include "wifi.h"

static const char *labels[] = { "Status", "Reconnect" };
#define COUNT (sizeof(labels) / sizeof(labels[0]))

uint8_t wifi_submenu_count(void) { return COUNT; }

const char *wifi_submenu_label(uint8_t index)
{
	return labels[index];
}
