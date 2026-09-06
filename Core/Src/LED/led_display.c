/*
 * led_display.c
 *
 *  Created on: Aug 25, 2026
 *      Author: ivan
 */

#include "led_display.h"
#include "tim.h"

#define COLON_BIT  (1U << 4)

/* One multiplex slot = one full TIM10 period (Period=1999 -> 2000 timer
   ticks, 1us/tick at the configured Prescaler=95 -> ~2ms/digit). Brightness
   is a software PWM on OE, synced to this same timer: TIM10's Update event
   (HAL_TIM_PeriodElapsedCallback, already driving multiplex_step()) turns
   OE on at the start of each slot, and its Output Compare channel 1
   (HAL_TIM_OC_DelayElapsedCallback, added here - doesn't drive any GPIO
   itself, "Timing" mode is purely an interrupt source) turns it back off
   partway through, proportional to level/LED_BRIGHTNESS_MAX.

   Level 10 (the top of the dial) is deliberately capped at 70% of the
   slot, not 100% - each digit is already only lit 1 of every 4 slots from
   multiplexing alone, and going further from there felt too intense at
   the "max" setting. Every level scales off that same 70% ceiling. */
#define MUX_SLOT_TICKS      2000
#define LED_BRIGHTNESS_CAP_PCT 40

static uint8_t brightness = 3;

static uint16_t brightness_to_ccr(uint8_t level)
{
	uint16_t cap_ticks = (uint16_t) ((uint32_t) MUX_SLOT_TICKS
			* LED_BRIGHTNESS_CAP_PCT / 100);

	if (level == 0) return 0;
	if (level >= LED_BRIGHTNESS_MAX) return cap_ticks;
	return (uint16_t) ((uint32_t) level * cap_ticks / LED_BRIGHTNESS_MAX);
}

/* digit slots hold 0-9 for real digits, or one of these symbolic glyphs -
   all indices into the same segment_map[] table below */
typedef enum {
	SEG_BLANK = 10, SEG_DASH, SEG_DEGREE, SEG_C, SEG_H, SEG_P,
	SEG_BAR1, SEG_BAR2, SEG_BAR3, SEG_BAR4,
	SEG_COUNT
} seg_glyph_t;

static volatile uint8_t digits[4] = { 0 };
static volatile uint8_t colon_on = 1;
static volatile uint8_t mux_index = 0;

/* bar levels climb the digit's outline bottom-to-top, skipping G (middle) -
   it's horizontal and doesn't read as "height" the way A/D do */
static const uint8_t segment_map[SEG_COUNT] = {
	0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, /* 0-9 */
	0x00, /* SEG_BLANK  -> off (also bar level 0) */
	0x40, /* SEG_DASH   -> "-" */
	0x63, /* SEG_DEGREE -> "°" */
	0x39, /* SEG_C      -> "C" */
	0x76, /* SEG_H      -> "H" */
	0x73, /* SEG_P      -> "P" */
	0x08, /* SEG_BAR1   -> D           (lowest) */
	0x1C, /* SEG_BAR2   -> D+E+C */
	0x3E, /* SEG_BAR3   -> D+E+C+F+B */
	0x3F, /* SEG_BAR4   -> D+E+C+F+B+A (highest, full outline) */
};

static uint8_t bar_glyph(uint8_t level)
{
	if (level >= 4) return SEG_BAR4;
	if (level == 3) return SEG_BAR3;
	if (level == 2) return SEG_BAR2;
	if (level == 1) return SEG_BAR1;
	return SEG_BLANK;
}

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
	uint8_t segment = segment_map[digits[i]];

	uint8_t ctrl = (1 << i);
	if (colon_on)
		ctrl |= COLON_BIT;
	segment |= 0x80;

	HAL_GPIO_WritePin(SR_OE_Port, SR_OE_Pin, GPIO_PIN_SET);   // OE_OFF (during shift)
	shift16(((uint16_t) ctrl << 8) | segment);

	if (brightness > 0)
		HAL_GPIO_WritePin(SR_OE_Port, SR_OE_Pin, GPIO_PIN_RESET); // OE_ON - HAL_TIM_OC_DelayElapsedCallback cuts this short at the current level's duty
	/* brightness == 0: leave OE off - blank slot */

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

	/* TIM_OCMODE_TIMING drives no pin at all - it's purely a second
	   interrupt source on the same already-running TIM10, used to cut the
	   OE_ON window short for brightness. Doesn't touch the .ioc/CubeMX
	   config; htim10 is already initialized as a running time base by
	   MX_TIM10_Init() before this runs. */
	TIM_OC_InitTypeDef sConfigOC = { 0 };
	sConfigOC.OCMode = TIM_OCMODE_TIMING;
	sConfigOC.Pulse = brightness_to_ccr(brightness);
	HAL_TIM_OC_ConfigChannel(&htim10, &sConfigOC, TIM_CHANNEL_1);
	HAL_TIM_OC_Start_IT(&htim10, TIM_CHANNEL_1);
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
	digits[0] = digits[1] = digits[2] = digits[3] = SEG_DASH;
}

void led_display_set_temp(int8_t temp_c)
{
	uint8_t t = (temp_c < 0) ? 0 : (uint8_t) temp_c;
	if (t > 99) t = 99;

	digits[0] = t / 10;
	digits[1] = t % 10;
	digits[2] = SEG_DEGREE;
	digits[3] = SEG_C;
	colon_on = 0;
}

void led_display_set_humidity(uint8_t rh_percent)
{
	uint8_t h = (rh_percent > 99) ? 99 : rh_percent;

	digits[0] = h / 10;
	digits[1] = h % 10;
	digits[2] = SEG_BLANK;
	digits[3] = SEG_H;
	colon_on = 0;
}

void led_display_set_pressure(uint16_t hpa)
{
	/* keep only the last 3 digits - our indoor range (roughly 950-1050
	   hPa) never needs the thousands digit to stay unambiguous, and this
	   leaves room for the "P" unit letter */
	uint16_t p3 = hpa % 1000;

	digits[0] = p3 / 100;
	digits[1] = (p3 / 10) % 10;
	digits[2] = p3 % 10;
	digits[3] = SEG_P;
	colon_on = 0;
}

void led_display_set_eq(uint8_t l0, uint8_t l1, uint8_t l2, uint8_t l3)
{
	digits[0] = bar_glyph(l0);
	digits[1] = bar_glyph(l1);
	digits[2] = bar_glyph(l2);
	digits[3] = bar_glyph(l3);
	colon_on = 0;
}

void led_brightness_set(uint8_t level)
{
	if (level > LED_BRIGHTNESS_MAX) level = LED_BRIGHTNESS_MAX;
	brightness = level;
	htim10.Instance->CCR1 = brightness_to_ccr(level);
}

uint8_t led_brightness_get(void) { return brightness; }

void led_brightness_adjust(int32_t delta)
{
	int32_t v = (int32_t) brightness + delta;
	if (v < 0) v = 0;
	if (v > LED_BRIGHTNESS_MAX) v = LED_BRIGHTNESS_MAX;
	led_brightness_set((uint8_t) v);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM10)
    {
        multiplex_step();
    }
}

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM10)
	{
		/* mid-slot cutoff for dimming - fires at every level, including
		   max (capped at LED_BRIGHTNESS_CAP_PCT of the slot, not 100%) */
		HAL_GPIO_WritePin(SR_OE_Port, SR_OE_Pin, GPIO_PIN_SET);
	}
}

