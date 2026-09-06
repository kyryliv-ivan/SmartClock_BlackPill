/*
 * oled.h
 *
 *  Created on: Aug 25, 2026
 *      Author: ivan
 */

#ifndef SRC_OLED_OLED_H_
#define SRC_OLED_OLED_H_

#include "main.h"

void oled_init(void);
void oled_clear(void);
void oled_line_large(uint8_t x, uint8_t y, const char *text); // Font_11*18
void oled_line_small(uint8_t x, uint8_t y, const char *text); // Font_7*10

/* bitmap: row-major, MSB-first, each row padded to a whole byte
   (same format as ssd1306_DrawBitmap / Adafruit-GFX) */
void oled_draw_bitmap(uint8_t x, uint8_t y, const uint8_t *bitmap, uint8_t w, uint8_t h);

void oled_flush(void);

#endif /* SRC_OLED_OLED_H_ */
