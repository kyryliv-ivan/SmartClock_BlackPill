#ifndef __AHT20_H__
#define __AHT20_H__

#include "main.h"

HAL_StatusTypeDef aht20_init(void);
HAL_StatusTypeDef aht20_read(float *temperature, float *humidity);

#endif /* __AHT20_H__ */
