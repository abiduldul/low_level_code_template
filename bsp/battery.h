#ifndef __BATTERY_H__
#define __BATTERY_H__

#include <stdint.h>

void Battery_Init();
void Battery_Sampling(void);
uint8_t Battery_GetPercentage();
float Battery_GetTemperature();

#endif