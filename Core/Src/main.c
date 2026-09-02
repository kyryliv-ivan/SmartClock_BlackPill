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
#include "adc.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "sensors.h"
#include "led_display.h"
#include "oled.h"
#include "menu.h"
#include "alarm.h"
#include "led_menu.h"
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

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

/* --- Battery level (PA5 / ADC1_IN5) --------------------------------------
 R1/R2 100k/100k divider off the raw battery net (B+/B-, tapped before
 the LX-2's boost converter) halves Vbat into ADC range, C1 100nF buffers
 the divider's high source impedance for the sample. Voltage->percent
 isn't linear for Li-ion, so it's a piecewise-linear fit over a measured
 discharge curve instead of a straight scale.

 BATTERY_CAL_SCALE corrects for real-board error the raw formula can't
 know about (R1/R2 tolerance, actual Vref vs the assumed 3.3V) - measured
 once against a multimeter: board read 3.20V computed while the pack was
 actually at 3.57V, so scale = 3.57 / 3.20. Re-measure and update this if
 R1/R2 ever get swapped for a different pair. */
#define BATTERY_CAL_SCALE 1.115f

static uint8_t battery_read_percent(void)
{
	static const struct {
		float v;
		uint8_t pct;
	} curve[] = { { 4.20f, 100 }, { 4.10f, 90 }, { 4.00f, 80 }, { 3.90f, 70 }, {
			3.80f, 60 }, { 3.70f, 45 }, { 3.60f, 25 }, { 3.50f, 15 },
			{ 3.30f, 5 }, { 3.20f, 0 }, };
	const int n = sizeof(curve) / sizeof(curve[0]);

	/* Average 16 samples instead of trusting a single conversion - the
	 divider's high source impedance makes one-shot readings noisy enough
	 to visibly jitter the displayed percent by a couple of points. */
	uint32_t sum = 0;
	for (int i = 0; i < 16; i++)
	{
		HAL_ADC_Start(&hadc1);
		HAL_StatusTypeDef status = HAL_ADC_PollForConversion(&hadc1, 10);
		sum += (status == HAL_OK) ? HAL_ADC_GetValue(&hadc1) : 0;
		HAL_ADC_Stop(&hadc1);
	}
	uint32_t raw = sum / 16;

	float v_bat = (raw / 4095.0f) * 3.3f * 2.0f * BATTERY_CAL_SCALE;

	if (v_bat >= curve[0].v)
		return curve[0].pct;
	if (v_bat <= curve[n - 1].v)
		return curve[n - 1].pct;

	for (int i = 0; i < n - 1; i++)
	{
		if (v_bat <= curve[i].v && v_bat >= curve[i + 1].v)
		{
			float span_v = curve[i].v - curve[i + 1].v;
			float span_p = (float) curve[i].pct - (float) curve[i + 1].pct;
			float frac = (v_bat - curve[i + 1].v) / span_v;
			return (uint8_t) (curve[i + 1].pct + frac * span_p);
		}
	}
	return 0;
}

/* --- Incoming link from ESP32 (TIME:/STATUS: lines) ---------------------
 USART1's NVIC interrupt wasn't enabled when USART1 was generated via
 CubeMX, so RX is polled from the main loop instead of interrupt-driven -
 same non-blocking style as everything else here. */

static char esp32_status[32] = "";

static void esp32_handle_line(const char *line)
{
	if (strncmp(line, "TIME:", 5) == 0)
	{
		int year, month, day, h, m, s;

		if (sscanf(line + 5, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &h, &m,
				&s) == 6)
		{
			/* Sync the RTC once, right after boot - DS3231 has its own
			 crystal and battery backup, it doesn't need per-second
			 correction, just an initial anchor to real time. */
			static uint8_t rtc_synced = 0;

			if (!rtc_synced)
			{
				time_set((uint8_t) h, (uint8_t) m, (uint8_t) day,
						(uint8_t) month, (uint8_t) (year % 100));
				rtc_synced = 1;
			}
		}
	} else if (strncmp(line, "STATUS:", 7) == 0)
	{
		/* e.g. "WIFI_OK,RADIO_PLAY" - kept for the WiFi submenu's Status
		 entry once that's wired up to actually display it. */
		strncpy(esp32_status, line + 7, sizeof(esp32_status) - 1);
		esp32_status[sizeof(esp32_status) - 1] = '\0';
	}
}

static void esp32_uart_poll(void)
{
	static char rx_line[40];
	static uint8_t rx_len = 0;

	if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE))
	{
		uint8_t c = (uint8_t) (huart1.Instance->DR & 0xFF);

		if (c == '\n')
		{
			rx_line[rx_len] = '\0';
			esp32_handle_line(rx_line);
			rx_len = 0;
		} else if (rx_len < sizeof(rx_line) - 1)
		{
			rx_line[rx_len++] = (char) c;
		}
		/* line too long - drop silently, rx_len stops growing until '\n' */
	}
}

void gpio_init(void)
{
	led_display_init();
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
	MX_USART1_UART_Init();
	MX_ADC1_Init();
	/* USER CODE BEGIN 2 */

	gpio_init();
	HAL_TIM_Base_Start_IT(&htim10);

	oled_init();

	sensors_init();
	led_menu_init();
	menu_init();

	/* slow-changing, so it's read on its own multi-second tick rather than
	 every clock redraw. battery_pct_f is a smoothed (EMA) running value -
	 the 16x-averaged reading from battery_read_percent() still moves a
	 point or two between updates, so this blends each new reading in
	 gradually instead of snapping the displayed number straight to it. */
	uint32_t battery_tick = 0;
	float battery_pct_f = (float) battery_read_percent();
	uint8_t battery_pct = (uint8_t) (battery_pct_f + 0.5f);

	char last_time[16] = "";

	/* USER CODE END 2 */

	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1)
	{

		esp32_uart_poll();

		/* --- Encoder / button -> UI state --------------------------------
		 Runs every pass (not gated to any tick) so the menu feels
		 responsive to rotation/tap, independent of the 200 ms RTC poll. */
		{
			int32_t pos_delta = encoder_delta_get();

			if (alarm_is_ringing())
			{
				if (encoder_tapped_get())
				{
					alarm_stop();
					last_time[0] = '\0';
				}
			}
			else
			{
				if (encoder_tapped_get())
					menu_tap();

				menu_rotate(pos_delta);
			}

			menu_tick();
			menu_draw();

			if (menu_consume_clock_redraw())
				last_time[0] = '\0';
		}

		/* Sensors - heavy I2C work (AHT20 has a blocking HAL_Delay(80)).
		 Spread bmp280/aht20/bh1750 across three separate ticks instead of
		 doing them back-to-back in one block every second. BH1750 is in
		 continuous mode, so its read is just a 2-byte fetch - no delay. */

		sensors_poll();

		if (HAL_GetTick() - battery_tick >= 5000)
		{
			battery_tick = HAL_GetTick();
			battery_pct_f = battery_pct_f * 0.7f
					+ (float) battery_read_percent() * 0.3f;
			battery_pct = (uint8_t) (battery_pct_f + 0.5f);
		}

		static uint32_t last_read = 0;

		if (HAL_GetTick() - last_read >= 200)
		{
			last_read = HAL_GetTick();

			if (!rtc_ok_get())
			{
				led_display_set_error();

				if (!menu_active())
				{
					oled_clear();
					oled_line_large(0, 24, "RTC ERROR");
					oled_flush();
				}
			} else
			{
				led_menu_tick();
				alarm_check(hour_get(), minute_get());

				char time_str[16];
				char date_str[16];

				sprintf(time_str, "%02d:%02d:%02d", hour_get(), minute_get(),
						second_get());

				if (alarm_is_ringing())
				{
					led_display_set_colon((HAL_GetTick() / 300) % 2);

					if (!menu_active())
					{
						oled_clear();
						oled_line_large(0, 20, "ALARM!");
						oled_line_small(0, 44, "tap to stop");
						oled_flush();
					}
				}
				else if (!menu_active() && strcmp(time_str, last_time) != 0)
				{
					strcpy(last_time, time_str);
					sprintf(date_str, "%02d.%02d.20%02d", day_get(),
							month_get(), year_get());

					char batt_str[8];
					sprintf(batt_str, "%3d%%", battery_pct);
					char sensor_str[16];

					oled_clear();
					oled_line_large(0, 0, time_str);
					oled_line_small(100, 4, batt_str);
					oled_line_small(0, 24, date_str);

					sprintf(sensor_str, "T:%.1fC H:%.0f%%", temperature_get(),
							humidity_get());
					oled_line_small(0, 38, sensor_str);

					sprintf(sensor_str, "P:%.0f L:%.0flx", pressure_get(),
							lux_get());
					oled_line_small(0, 52, sensor_str);
					oled_flush();
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
	RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };

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
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
			| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
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
