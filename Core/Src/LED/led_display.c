/*
 * led_display.c
 *
 *  Created on: Aug 25, 2026
 *      Author: ivan
 */

#include "led_display.h"

#define BLANK      0xFF
#define ERR_DASH   0xFE
#define COLON_BIT  (1U << 4)

static volatile uint8_t digits[4] = { 0 };
static volatile uint8_t colon_on = 1;
static volatile uint8_t mux_index = 0;

static const uint8_t segment_map[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D,
		0x7D, 0x07, 0x7F, 0x6F };

static void shift16(uint16_t value)
{
	for (int i = 15; i >= 0; i--)
	{
		HAL_GPIO_WritePin(SR_GPIO_Port, SR_DATA_Pin,
				(value & (1U << i)) ? GPIO_PIN_SET : GPIO_PIN_RESET);

		HAL_GPIO_WritePin(SR_GPIO_Port, SR_CLK_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(SR_GPIO_Port, SR_CLK_Pin, GPIO_PIN_RESET);
	}

	HAL_GPIO_WritePin(SR_GPIO_Port, SR_LATCH_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(SR_GPIO_Port, SR_LATCH_Pin, GPIO_PIN_RESET);
}

static void multiplex_step(void)
{
	uint8_t i = mux_index;
	uint8_t segment;

	if (digits[i] < 10)
		segment = segment_map[digits[i]];
	else if (digits[i] == ERR_DASH)
		segment = 0x40;
	else
		segment = 0x00;

	uint8_t ctrl = (1 << i);
	if (colon_on)
		ctrl |= COLON_BIT;
	segment |= 0x80;

	HAL_GPIO_WritePin(SR_OE_Port, SR_OE_Pin, GPIO_PIN_SET);   // OE_OFF
	shift16(((uint16_t) ctrl << 8) | segment);
	HAL_GPIO_WritePin(SR_OE_Port, SR_OE_Pin, GPIO_PIN_RESET); // OE_ON

	mux_index = (i + 1) & 0x03;
}

void led_display_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	GPIO_InitStruct.Pin = SR_DATA_Pin | SR_CLK_Pin | SR_LATCH_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(SR_GPIO_Port, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = SR_OE_Pin;
	HAL_GPIO_Init(SR_OE_Port, &GPIO_InitStruct);

	HAL_GPIO_WritePin(SR_OE_Port, SR_OE_Pin, GPIO_PIN_SET); // OE_OFF start
}

void led_display_set_time(uint8_t hours, uint8_t minutes)
{
	digits[0] = hours / 10;
	digits[1] = hours % 10;
	digits[2] = minutes / 10;
	digits[3] = minutes % 10;
}

void led_display_set_colon(uint8_t on)
{
	colon_on = on;
}

void led_display_set_error(void)
{
	digits[0] = digits[1] = digits[2] = digits[3] = ERR_DASH;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM10)
    {
        multiplex_step();
    }
}

