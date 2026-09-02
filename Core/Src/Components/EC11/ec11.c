#include "ec11.h"

#define EC11_DIR_CW  0x10
#define EC11_DIR_CCW 0x20

#define EC11_R_START     0x0
#define EC11_R_CW_FINAL  0x1
#define EC11_R_CW_BEGIN  0x2
#define EC11_R_CW_NEXT   0x3
#define EC11_R_CCW_BEGIN 0x4
#define EC11_R_CCW_FINAL 0x5
#define EC11_R_CCW_NEXT  0x6

static const uint8_t ec11_ttable[7][4] =
{
    { EC11_R_START,    EC11_R_CW_BEGIN,  EC11_R_CCW_BEGIN, EC11_R_START },
    { EC11_R_CW_NEXT,  EC11_R_START,     EC11_R_CW_FINAL,  EC11_R_START | EC11_DIR_CW },
    { EC11_R_CW_NEXT,  EC11_R_CW_BEGIN,  EC11_R_START,     EC11_R_START },
    { EC11_R_CW_NEXT,  EC11_R_CW_BEGIN,  EC11_R_CW_FINAL,  EC11_R_START },
    { EC11_R_CCW_NEXT, EC11_R_START,     EC11_R_CCW_BEGIN, EC11_R_START },
    { EC11_R_CCW_NEXT, EC11_R_CCW_FINAL, EC11_R_START,     EC11_R_START | EC11_DIR_CCW },
    { EC11_R_CCW_NEXT, EC11_R_CCW_FINAL, EC11_R_CCW_BEGIN, EC11_R_START },
};

static volatile int32_t  ec11_position      = 0;
static volatile uint8_t  ec11_state         = EC11_R_START;
static volatile uint8_t  last_ab            = 0xFF;
static volatile uint32_t last_rotation_tick = 0;

static volatile uint8_t  sw_down          = 0;
static volatile uint8_t  button_tapped    = 0;
static volatile uint32_t last_button_tick = 0;

void ec11_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOB_CLK_ENABLE();

	GPIO_InitStruct.Pin = ENC_CLK_Pin | ENC_DT_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(ENC_GPIO_Port, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = ENC_SW_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(ENC_GPIO_Port, &GPIO_InitStruct);

	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 3, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

int32_t ec11_get_delta(void)
{
	static int32_t last_position = 0;
	int32_t now = ec11_position;
	int32_t delta = now - last_position;
	last_position = now;
	return delta;
}

uint8_t ec11_button_tapped(void)
{
	if (button_tapped)
	{
		button_tapped = 0;
		return 1;
	}
	return 0;
}

static void ec11_handle_rotation_edge(void)
{
	if (HAL_GPIO_ReadPin(ENC_GPIO_Port, ENC_SW_Pin) == GPIO_PIN_RESET)
		return;

	uint8_t a = HAL_GPIO_ReadPin(ENC_GPIO_Port, ENC_CLK_Pin);
	uint8_t b = HAL_GPIO_ReadPin(ENC_GPIO_Port, ENC_DT_Pin);
	uint8_t ab = (a << 1) | b;

	if (ab == last_ab)
		return;
	last_ab = ab;

	last_rotation_tick = HAL_GetTick();

	ec11_state = ec11_ttable[ec11_state & 0x0F][ab];

	uint8_t dir = ec11_state & 0x30;
	if (dir == EC11_DIR_CW)
		ec11_position++;
	if (dir == EC11_DIR_CCW)
		ec11_position--;
}

static void ec11_handle_button_edge(void)
{
	uint32_t now = HAL_GetTick();
	uint8_t level = HAL_GPIO_ReadPin(ENC_GPIO_Port, ENC_SW_Pin);

	if (level == GPIO_PIN_RESET)
	{
		if (now - last_rotation_tick < 10)
			return;

		if (!sw_down && (now - last_button_tick >= 200))
		{
			sw_down = 1;
			last_button_tick = now;
			button_tapped = 1;
		}
	} else
	{
		sw_down = 0;
	}
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == ENC_CLK_Pin || GPIO_Pin == ENC_DT_Pin)
		ec11_handle_rotation_edge();
	else if (GPIO_Pin == ENC_SW_Pin)
		ec11_handle_button_edge();
}
