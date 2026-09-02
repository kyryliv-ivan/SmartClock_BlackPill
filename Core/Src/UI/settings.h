#ifndef SETTINGS_H
#define SETTINGS_H

#include "main.h"

uint8_t     settings_submenu_count(void);
const char *settings_submenu_label(uint8_t index);

typedef enum {
	SETTINGS_ACTION_EDIT_TIME,
	SETTINGS_ACTION_EDIT_DATE,
	SETTINGS_ACTION_LED_SELECT,
	SETTINGS_ACTION_LED_INTERVAL,
	SETTINGS_ACTION_VOLUME,
} settings_action_t;

settings_action_t settings_submenu_tap(uint8_t index);

#endif /* SETTINGS_H */
