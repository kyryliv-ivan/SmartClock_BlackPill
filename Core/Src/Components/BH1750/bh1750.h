#ifndef __BH1750_H__
#define __BH1750_H__

#include "main.h"

HAL_StatusTypeDef bh1750_init(void);
HAL_StatusTypeDef bh1750_read(float *lux);

#endif /* __BH1750_H__ */
