#ifndef STOPWATCH_H
#define STOPWATCH_H

#include "main.h"

void     stopwatch_reset(void);
void     stopwatch_start_stop(void);
uint8_t  stopwatch_running_get(void);
uint32_t stopwatch_elapsed_ms_get(void);

#endif /* STOPWATCH_H */
