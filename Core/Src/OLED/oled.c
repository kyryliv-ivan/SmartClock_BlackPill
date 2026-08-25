/*
 * oled.c
 *
 *  Created on: Aug 25, 2026
 *      Author: ivan
 */

#include "oled.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"

void oled_init(void)
{
	ssd1306_Init();
}

void oled_clear(void)
{
	ssd1306_Fill(Black);
}

void oled_line_large(uint8_t x, uint8_t y, const char *text)
{
	ssd1306_SetCursor(x, y);
	ssd1306_WriteString((char*) text, Font_11x18, White);
}

void oled_line_small(uint8_t x, uint8_t y, const char *text)
{
	ssd1306_SetCursor(x, y);
	ssd1306_WriteString((char*) text, Font_7x10, White);
}

void oled_flush(void)
{
	ssd1306_UpdateScreen();
}
