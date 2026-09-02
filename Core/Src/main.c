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
#include "time_editor.h"
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

/* --- OLED menu (opened by a tap on the encoder) -------------------------
 Font_7x10/Font_11x18 only cover ASCII, so labels stay in Latin/English -
 there is no Cyrillic glyph data in ssd1306_fonts.

 Three levels: CLOCK -> MENU (Alarm/Radio/WiFi, rotate to move) -> tap
 opens that item's own list (SUBMENU, rotate to move) -> tap on a list
 entry performs the action and drops straight back to CLOCK. */
typedef enum {
	UI_CLOCK, UI_MENU, UI_SUBMENU, UI_EDIT
} ui_mode_t;
typedef enum {
	MENU_ALARM, MENU_RADIO, MENU_WIFI, MENU_SETTINGS, MENU_COUNT
} menu_item_t;

static const char *menu_labels[MENU_COUNT] = { "Alarm", "Radio", "WiFi", "Settings" };

/* rows visible at once in the top-level menu at Font_11x18's 20px pitch
   on a 64px-tall display (0, 20, 44 -> last row ends at 62) */
#define MENU_VISIBLE_ROWS 3

static const char *settings_labels[] = { "Set Time", "Set Date" };
#define SETTINGS_COUNT (sizeof(settings_labels) / sizeof(settings_labels[0]))

/* Radio station names - must stay in the same order as `stations[]` on the
 ESP32 side (see ESP32/SmartClock_ESP32/SmartClock_ESP32.ino), since only
 the index is sent over UART, never the name. */
static const char *radio_labels[] = { "Hit FM", "Radio ROKS", "Kiss FM" };
#define RADIO_COUNT (sizeof(radio_labels) / sizeof(radio_labels[0]))

/* Placeholders - real Alarm/WiFi behaviour (time editor, status readout,
 ...) is still to be designed; these just make the 3-level menu testable
 end-to-end today. */
static const char *alarm_labels[] = { "Off", "On" };
#define ALARM_COUNT (sizeof(alarm_labels) / sizeof(alarm_labels[0]))

static const char *wifi_labels[] = { "Status", "Reconnect" };
#define WIFI_COUNT (sizeof(wifi_labels) / sizeof(wifi_labels[0]))

static const char* const* submenu_labels(menu_item_t item, uint8_t *count)
{
	switch (item)
	{
	case MENU_ALARM:
		*count = ALARM_COUNT;
		return alarm_labels;
	case MENU_RADIO:
		*count = RADIO_COUNT;
		return radio_labels;
	case MENU_WIFI:
		*count = WIFI_COUNT;
		return wifi_labels;
	case MENU_SETTINGS:
		*count = SETTINGS_COUNT;
		return settings_labels;
	default:
		*count = 0;
		return NULL;
	}
}

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

static void send_station_to_esp32(uint8_t index)
{
	char line[16];
	int len = snprintf(line, sizeof(line), "STATION:%u\n", (unsigned) index);

	/* rare, user-triggered event - a short blocking send is fine here */
	HAL_UART_Transmit(&huart1, (uint8_t*) line, (uint16_t) len, 100);
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

	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	led_display_init();

	/* Rotary encoder (EC11) on PB12/13/14. CLK/DT sit on TIM1's
	 complementary channels (CH1N/CH2N) on this package, not the primary
	 TI1/TI2 inputs that hardware Encoder Mode needs - so both edges are
	 decoded in software via EXTI instead. SW is a plain active-low
	 button (pressed = pulled to GND). */
	GPIO_InitStruct.Pin = ENC_CLK_Pin | ENC_DT_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(ENC_GPIO_Port, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = ENC_SW_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(ENC_GPIO_Port, &GPIO_InitStruct);

	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 3, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* --- Rotary encoder (EC11), robust state-machine quadrature decode ------
 Classic "full-step" design (Ben Buxton / Peter Dannegger table): unlike
 a simple accumulate-to-4 counter, this only registers a step once the
 encoder has passed through the FULL valid transition sequence and
 settled back into a rest state. Contact bounce or a reversal mid-click
 just resets to "waiting" instead of corrupting a running sum - far more
 resistant to a noisy/unfiltered signal than a plain accumulator. */

#define ENC_DIR_CW  0x10
#define ENC_DIR_CCW 0x20

#define ENC_R_START     0x0
#define ENC_R_CW_FINAL  0x1
#define ENC_R_CW_BEGIN  0x2
#define ENC_R_CW_NEXT   0x3
#define ENC_R_CCW_BEGIN 0x4
#define ENC_R_CCW_FINAL 0x5
#define ENC_R_CCW_NEXT  0x6

/* rows = current state (masked to 3 bits), columns = (A<<1)|B */
static const uint8_t encoder_ttable[7][4] = { { ENC_R_START, ENC_R_CW_BEGIN,
ENC_R_CCW_BEGIN, ENC_R_START }, { ENC_R_CW_NEXT, ENC_R_START,
ENC_R_CW_FINAL, ENC_R_START | ENC_DIR_CW }, { ENC_R_CW_NEXT,
ENC_R_CW_BEGIN, ENC_R_START, ENC_R_START }, { ENC_R_CW_NEXT,
ENC_R_CW_BEGIN, ENC_R_CW_FINAL, ENC_R_START }, { ENC_R_CCW_NEXT,
ENC_R_START, ENC_R_CCW_BEGIN, ENC_R_START }, { ENC_R_CCW_NEXT,
ENC_R_CCW_FINAL, ENC_R_START, ENC_R_START | ENC_DIR_CCW }, {
ENC_R_CCW_NEXT, ENC_R_CCW_FINAL, ENC_R_CCW_BEGIN, ENC_R_START }, };

static volatile int32_t enc_position = 0;
static volatile uint8_t enc_state = ENC_R_START;
static volatile uint8_t last_ab = 0xFF; /* sentinel - forces first call through */
static volatile uint32_t last_rotation_tick = 0;

static volatile uint8_t button_tapped = 0;
static volatile uint32_t last_button_tick = 0;

static void encoder_update(void)
{
	/* Ignore rotation while the button is physically held down - the push
	   switch and the rotary contacts share one shaft on cheap EC11
	   modules, so pressing (especially holding a bit longer) can wobble
	   the shaft enough to register as an accidental extra step alongside
	   the tap, making one press feel like two actions. */
	if (HAL_GPIO_ReadPin(ENC_GPIO_Port, ENC_SW_Pin) == GPIO_PIN_RESET)
		return;

	uint8_t a = HAL_GPIO_ReadPin(ENC_GPIO_Port, ENC_CLK_Pin);
	uint8_t b = HAL_GPIO_ReadPin(ENC_GPIO_Port, ENC_DT_Pin);
	uint8_t ab = (a << 1) | b;

	/* EXTI fired but the pins already read back as unchanged (a very
	 brief glitch) - nothing to decode, skip the table lookup. Also
	 skip the "electrically active" timestamp below for this case:
	 if it were updated unconditionally, a noisy/undebounced encoder
	 could keep re-arming the SW reject window in HAL_GPIO_EXTI_Callback
	 on phantom edges alone and permanently swallow every button tap. */
	if (ab == last_ab)
		return;
	last_ab = ab;

	/* Marks "the encoder is electrically active right now" on every real
	 CLK/DT transition (including bounce, not just confirmed steps) -
	 crosstalk onto SW happens on these raw edges, not only on completed
	 clicks, so this needs to catch them all. The SW window below is kept
	 short (see HAL_GPIO_EXTI_Callback) so this doesn't eat a deliberate
	 tap that follows shortly after rotation. */
	last_rotation_tick = HAL_GetTick();

	enc_state = encoder_ttable[enc_state & 0x0F][ab];

	uint8_t dir = enc_state & 0x30;
	if (dir == ENC_DIR_CW)
		enc_position++;
	if (dir == ENC_DIR_CCW)
		enc_position--;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == ENC_CLK_Pin || GPIO_Pin == ENC_DT_Pin)
	{
		encoder_update();
	} else if (GPIO_Pin == ENC_SW_Pin)
	{
		uint32_t now = HAL_GetTick();

		/* Reject SW edges that land right on top of CLK/DT activity -
		 almost certainly electrical crosstalk, not an intentional press.
		 Kept short on purpose: real crosstalk rides on the same raw
		 edge/bounce train (settles within a few ms), while a deliberate
		 "rotate then tap" gesture takes much longer than that for a
		 human to actually press - so a short window catches the former
		 without eating the latter. */
		if (now - last_rotation_tick < 10)
			return;

		/* debounce - ignore repeat presses within 200 ms of the last one */
		if (now - last_button_tick >= 200)
		{
			last_button_tick = now;
			button_tapped = 1;
		}
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
	MX_USART1_UART_Init();
	MX_ADC1_Init();
	/* USER CODE BEGIN 2 */

	gpio_init();
	HAL_TIM_Base_Start_IT(&htim10);

	oled_init();

	sensors_init();

	/* slow-changing, so it's read on its own multi-second tick rather than
	 every clock redraw. battery_pct_f is a smoothed (EMA) running value -
	 the 16x-averaged reading from battery_read_percent() still moves a
	 point or two between updates, so this blends each new reading in
	 gradually instead of snapping the displayed number straight to it. */
	uint32_t battery_tick = 0;
	float battery_pct_f = (float) battery_read_percent();
	uint8_t battery_pct = (uint8_t) (battery_pct_f + 0.5f);

	char last_time[16] = "";

	ui_mode_t ui_mode = UI_CLOCK;
	TimeEditor_t time_ed;
	int32_t last_enc_position = enc_position;
	int8_t menu_index = 0;
	int8_t submenu_index = 0;
	uint32_t menu_last_activity = 0;
	uint8_t menu_needs_redraw = 0;

	/* remembers the last entry picked in each submenu (e.g. which radio
	 station is currently playing), so reopening a submenu highlights the
	 current choice instead of always starting back at index 0 */
	int8_t last_submenu_index[MENU_COUNT] = { 0 };

	//set itme / date
//
//   time_set(11, 25, 18, 8, 26);

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
			int32_t pos_now = enc_position;
			int32_t pos_delta = pos_now - last_enc_position;
			last_enc_position = pos_now;

			if (button_tapped)
			{
				button_tapped = 0;
				menu_last_activity = HAL_GetTick();

				if (ui_mode == UI_CLOCK)
				{
					ui_mode = UI_MENU;
					menu_index = 0;
					menu_needs_redraw = 1;
				} else if (ui_mode == UI_MENU)
				{
					/* enter the chosen top-level item's own list, highlighting
					 whatever was picked there last time */
					ui_mode = UI_SUBMENU;
					submenu_index = last_submenu_index[menu_index];
					menu_needs_redraw = 1;
				} else if (ui_mode == UI_SUBMENU) /* list entry picked */
				{
					uint8_t count;
					const char *const*labels = submenu_labels(
							(menu_item_t) menu_index, &count);

					last_submenu_index[menu_index] = submenu_index;

					if ((menu_item_t) menu_index == MENU_SETTINGS)
					{
						if (submenu_index == 0)
							time_editor_start_time(&time_ed);
						else
							time_editor_start_date(&time_ed);
						ui_mode = UI_EDIT;
						menu_needs_redraw = 1;
					}
					else
					{
						if ((menu_item_t) menu_index == MENU_RADIO)
						{
							send_station_to_esp32((uint8_t) submenu_index);
						}
						/* MENU_ALARM / MENU_WIFI actions land here once their
						 real behaviour (status/reconnect, ...) is decided. */

						oled_clear();
						oled_line_large(0, 24, labels[submenu_index]);
						oled_line_small(0, 48, "selected");
						oled_flush();

						HAL_Delay(600);

						ui_mode = UI_CLOCK;
						last_time[0] = '\0'; /* force a clock redraw on next tick */
					}
				} else /* UI_EDIT: field confirmed via tap */
				{
					if (time_editor_tap(&time_ed))
					{
						time_editor_commit(&time_ed);
						ui_mode = UI_CLOCK;
						last_time[0] = '\0';
					}
					menu_needs_redraw = 1;
				}
			}

			if (ui_mode == UI_MENU && pos_delta != 0)
			{
				int32_t idx = ((menu_index + pos_delta) % MENU_COUNT
						+ MENU_COUNT) % MENU_COUNT;
				menu_index = (int8_t) idx;
				menu_needs_redraw = 1;
				menu_last_activity = HAL_GetTick();
			}

			if (ui_mode == UI_SUBMENU && pos_delta != 0)
			{
				uint8_t count;
				submenu_labels((menu_item_t) menu_index, &count);

				int32_t idx = ((submenu_index + pos_delta) % count + count)
						% count;
				submenu_index = (int8_t) idx;
				menu_needs_redraw = 1;
				menu_last_activity = HAL_GetTick();
			}

			if (ui_mode == UI_EDIT && pos_delta != 0)
			{
				time_editor_rotate(&time_ed, pos_delta);
				menu_needs_redraw = 1;
				menu_last_activity = HAL_GetTick();
			}

			/* auto-return to the clock after a few seconds of inactivity,
			 from either menu level */
			if (ui_mode != UI_CLOCK
					&& HAL_GetTick() - menu_last_activity >= 6000)
			{
				ui_mode = UI_CLOCK;
				last_time[0] = '\0';
			}

			if (ui_mode == UI_MENU && menu_needs_redraw)
			{
				menu_needs_redraw = 0;

				/* Only 3 rows fit on a 64px-tall display at Font_11x18's
				   20px pitch (0, 20, 44 -> last row ends at 62). With more
				   menu items than that, scroll a 3-row window so it always
				   contains menu_index - recomputed fresh each redraw from
				   menu_index alone, no extra persistent state needed. */
				uint8_t window_start = 0;
				if (menu_index >= MENU_VISIBLE_ROWS)
					window_start = menu_index - MENU_VISIBLE_ROWS + 1;
				if (MENU_COUNT > MENU_VISIBLE_ROWS
						&& window_start > MENU_COUNT - MENU_VISIBLE_ROWS)
					window_start = MENU_COUNT - MENU_VISIBLE_ROWS;

				oled_clear();
				for (uint8_t row = 0;
						row < MENU_VISIBLE_ROWS
								&& (window_start + row) < MENU_COUNT; row++)
				{
					uint8_t i = window_start + row;
					oled_line_large(0, row * 20 + 4, i == menu_index ? ">" : " ");
					oled_line_large(16, row * 20 + 4, menu_labels[i]);
				}
				oled_flush();
			}

			if (ui_mode == UI_SUBMENU && menu_needs_redraw)
			{
				menu_needs_redraw = 0;

				uint8_t count;
				const char *const*labels = submenu_labels(
						(menu_item_t) menu_index, &count);

				/* one row per entry - fine up to 4 items in the remaining
				 54 px; a longer list (e.g. more radio stations) would
				 need scrolling, not needed yet */
				oled_clear();
				oled_line_small(0, 0, menu_labels[menu_index]);

				for (uint8_t i = 0; i < count && i < 4; i++)
				{
					oled_line_small(0, 14 + i * 12,
							i == submenu_index ? ">" : " ");
					oled_line_small(10, 14 + i * 12, labels[i]);
				}
				oled_flush();
			}

			if (ui_mode == UI_EDIT && menu_needs_redraw)
			{
				menu_needs_redraw = 0;
				time_editor_draw(&time_ed);
			}
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

				if (ui_mode == UI_CLOCK)
				{
					oled_clear();
					oled_line_large(0, 24, "RTC ERROR");
					oled_flush();
				}
			} else
			{
				led_display_set_time(hour_get(), minute_get());
				led_display_set_colon(1);

				char time_str[16];
				char date_str[16];

				sprintf(time_str, "%02d:%02d:%02d", hour_get(), minute_get(),
						second_get());

				if (ui_mode == UI_CLOCK && strcmp(time_str, last_time) != 0)
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
