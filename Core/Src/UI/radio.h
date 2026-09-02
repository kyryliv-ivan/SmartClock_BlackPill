#ifndef RADIO_H
#define RADIO_H

#include "main.h"

uint8_t     radio_submenu_count(void);
const char *radio_submenu_label(uint8_t index);
void        radio_submenu_tap(uint8_t index);

#endif /* RADIO_H */
