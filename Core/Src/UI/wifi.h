#ifndef WIFI_H
#define WIFI_H

#include "main.h"

uint8_t     wifi_submenu_count(void);
const char *wifi_submenu_label(uint8_t index);

typedef enum { WIFI_ACTION_NONE, WIFI_ACTION_FORGET } wifi_action_t;
wifi_action_t wifi_submenu_tap(uint8_t index);

/* state, populated from the ESP32's STATUS: line (see main.c) */
void        wifi_connected_set(uint8_t connected);
uint8_t     wifi_connected_get(void);
void        wifi_ssid_set(const char *ssid);
const char *wifi_ssid_get(void);

/* QR screen - static bitmap, no rotate/tap logic needed. Shown after
   Forget WiFi and automatically while the ESP32 is in setup mode. */
void wifi_qr_draw(void);

/* set from the ESP32's SETUP_MODE: line (see main.c) - true while it's
   running the SmartClock setup AP instead of being connected */
void    wifi_setup_mode_set(uint8_t on);
uint8_t wifi_setup_mode_get(void);

#endif /* WIFI_H */
