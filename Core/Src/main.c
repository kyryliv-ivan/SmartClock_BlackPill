/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */


#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <stdio.h>
#include <string.h>
#include "aht20.h"
#include "bmp280.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */


#define BLANK     0xFF
#define ERR_DASH  0xFE
#define COLON_BIT (1U << 4)

typedef struct
{
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} Time;

typedef struct
{
    uint8_t day;
    uint8_t month;
    uint8_t year;
} Date;

HAL_StatusTypeDef ds3231_read(Time *t, Date *d);
HAL_StatusTypeDef ds3231_set_time(Time t, Date d);

static uint8_t to_bcd(uint8_t v)   { return ((v / 10) << 4) | (v % 10); }
static uint8_t from_bcd(uint8_t v) { return ((v >> 4) * 10) + (v & 0x0F); }

HAL_StatusTypeDef ds3231_read(Time *t, Date *d)
{
    uint8_t buf[7];
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Read(&hi2c1, DS3231_ADDR, 0x00,
                               I2C_MEMADD_SIZE_8BIT, buf, 7, HAL_MAX_DELAY);
    if (status != HAL_OK) return status;

    t->seconds = from_bcd(buf[0] & 0x7F);
    t->minutes = from_bcd(buf[1]);
    t->hours   = from_bcd(buf[2] & 0x3F);

    d->day     = from_bcd(buf[4]);
    d->month   = from_bcd(buf[5] & 0x1F);
    d->year    = from_bcd(buf[6]);

    return HAL_OK;
}

HAL_StatusTypeDef ds3231_set_time(Time t, Date d)
{
    uint8_t buf[7];

    buf[0] = to_bcd(t.seconds);
    buf[1] = to_bcd(t.minutes);
    buf[2] = to_bcd(t.hours);
    buf[3] = 1;
    buf[4] = to_bcd(d.day);
    buf[5] = to_bcd(d.month);
    buf[6] = to_bcd(d.year);

    return HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDR, 0x00,
                              I2C_MEMADD_SIZE_8BIT, buf, 7, HAL_MAX_DELAY);
}

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


static volatile uint8_t digits[4] = {0};
static volatile uint8_t colon_on = 1;
static volatile uint8_t mux_index = 0;

static const uint8_t segment_map[10] =
{
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

void gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin   = SR_DATA_Pin | SR_CLK_Pin | SR_LATCH_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SR_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = SR_OE_Pin;
    HAL_GPIO_Init(SR_OE_Port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(SR_OE_Port, SR_OE_Pin, GPIO_PIN_SET); // OE_OFF start
}

void shift16(uint16_t value)
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

void multiplex_step(void)
{
    uint8_t i = mux_index;
    uint8_t segment;

    if (digits[i] < 10) segment = segment_map[digits[i]];
    else if (digits[i] == ERR_DASH) segment = 0x40;
    else segment = 0x00;

    uint8_t ctrl = (1 << i);
    if (colon_on) ctrl |= COLON_BIT;
    segment |= 0x80;

    HAL_GPIO_WritePin(SR_OE_Port, SR_OE_Pin, GPIO_PIN_SET);   // OE_OFF
    shift16(((uint16_t)ctrl << 8) | segment);
    HAL_GPIO_WritePin(SR_OE_Port, SR_OE_Pin, GPIO_PIN_RESET); // OE_ON

    mux_index = (i + 1) & 0x03;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM10)
    {
        multiplex_step();
    }
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM10_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */


  gpio_init();
  HAL_TIM_Base_Start_IT(&htim10);

  ssd1306_Init();
  ssd1306_Fill(Black);
  ssd1306_UpdateScreen();

  bmp280_init();
  aht20_init();

  float bmpTemp = 0.0f;
  float pressure = 0.0f;
  float ahtTemp = 0.0f;
  float humidity = 0.0f;

  uint32_t sensor_tick = 0;
  uint8_t  sensor_step = 0;


  //set itme / date
//
//   Time t = { .hours = 11, .minutes = 25, .seconds = 0 };
//   Date d = { .day = 18, .month = 8, .year = 26 };
//   ds3231_set_time(t, d);


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {


	  /* Sensors - heavy I2C work (AHT20 has a blocking HAL_Delay(80)).
	     Spread bmp280/aht20 across two separate ticks instead of
	     doing both back-to-back in one block every second. */
	  if (HAL_GetTick() - sensor_tick >= 700)
	  {
	      sensor_tick = HAL_GetTick();

	      switch (sensor_step)
	      {
	          case 0:
	              bmp280_read(&bmpTemp, &pressure);
	              break;

	          case 1:
	              aht20_read(&ahtTemp, &humidity);
	              break;
	      }

	      sensor_step = (sensor_step + 1) % 2;
	  }

	  static uint32_t last_read = 0;

	  if (HAL_GetTick() - last_read >= 200)
	  {
	      last_read = HAL_GetTick();

	      Time t;
	      Date d;
	      if (ds3231_read(&t, &d) == HAL_OK)
	      {
				digits[0] = t.hours / 10;
				digits[1] = t.hours % 10;
				digits[2] = t.minutes / 10;
				digits[3] = t.minutes % 10;
				colon_on = 1;

				static char last_time[16] = "";
				char time_str[16];
				char date_str[16];

				sprintf(time_str, "%02d:%02d:%02d", t.hours, t.minutes,
						t.seconds);

				if (strcmp(time_str, last_time) != 0) {
					strcpy(last_time, time_str);

					sprintf(date_str, "%02d.%02d.20%02d", d.day, d.month,
							d.year);

					ssd1306_Fill(Black);

					ssd1306_SetCursor(0, 0);
					ssd1306_WriteString(time_str, Font_11x18, White);

					ssd1306_SetCursor(0, 24);
					ssd1306_WriteString(date_str, Font_7x10, White);

					char sensor_str[16];

					sprintf(sensor_str, "T:%.1fC H:%.0f%%", ahtTemp, humidity);
					ssd1306_SetCursor(0, 38);
					ssd1306_WriteString(sensor_str, Font_7x10, White);

					sprintf(sensor_str, "P:%.1f hPa", pressure);
					ssd1306_SetCursor(0, 52);
					ssd1306_WriteString(sensor_str, Font_7x10, White);

					ssd1306_UpdateScreen();
				}
			}
	  }



    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
