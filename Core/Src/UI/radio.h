#ifndef RADIO_H
#define RADIO_H

#include "main.h"

uint8_t     radio_submenu_count(void);
const char *radio_submenu_label(uint8_t index);
void        radio_submenu_tap(uint8_t index);

#define RADIO_VOLUME_MAX 10

void    radio_volume_set(uint8_t vol);      /* clamped to 0..RADIO_VOLUME_MAX */
uint8_t radio_volume_get(void);
void    radio_volume_adjust(int32_t delta); /* clamped +/- step */

#endif /* RADIO_H */
