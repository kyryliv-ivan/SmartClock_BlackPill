#include "stopwatch.h"

static uint32_t start_tick     = 0;
static uint32_t accumulated_ms = 0;
static uint8_t  running        = 0;

void stopwatch_reset(void)
{
	running        = 0;
	accumulated_ms = 0;
}

void stopwatch_start_stop(void)
{
	if (running)
	{
		accumulated_ms += HAL_GetTick() - start_tick;
		running = 0;
	}
	else
	{
		start_tick = HAL_GetTick();
		running = 1;
	}
}

uint8_t stopwatch_running_get(void) { return running; }

uint32_t stopwatch_elapsed_ms_get(void)
{
	return running ? accumulated_ms + (HAL_GetTick() - start_tick) : accumulated_ms;
}
