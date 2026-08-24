/*
 * sensors.h
 *
 *  Created on: Aug 24, 2026
 *      Author: ivan
 */

#ifndef SRC_SENSORS_SENSORS_H_
#define SRC_SENSORS_SENSORS_H_

void sensors_init(void);
void sensors_poll(void);

float temperature_get(void);
float humidity_get(void);
float pressure_get(void);
float lux_get(void);


#endif /* SRC_SENSORS_SENSORS_H_ */
