#ifndef __BMP280_H__
#define __BMP280_H__

#include "main.h"

HAL_StatusTypeDef bmp280_init(void);
HAL_StatusTypeDef bmp280_read(float *temperature, float *pressure_hpa);

#endif /* __BMP280_H__ */
