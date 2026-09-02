#include "settings.h"

static const char *labels[] = { "Set Time", "Set Date", "LED Display", "LED Interval" };
#define COUNT (sizeof(labels) / sizeof(labels[0]))

uint8_t settings_submenu_count(void) { return COUNT; }

const char *settings_submenu_label(uint8_t index)
{
	return labels[index];
}

settings_action_t settings_submenu_tap(uint8_t index)
{
	switch (index)
	{
	case 0:  return SETTINGS_ACTION_EDIT_TIME;
	case 1:  return SETTINGS_ACTION_EDIT_DATE;
	case 2:  return SETTINGS_ACTION_LED_SELECT;
	default: return SETTINGS_ACTION_LED_INTERVAL;
	}
}
