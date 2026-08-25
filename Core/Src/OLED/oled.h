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
void oled_flush(void);

#endif /* SRC_OLED_OLED_H_ */
