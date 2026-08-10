#ifndef __PIN_RAINFALL_H__
#define __PIN_RAINFALL_H__

#include <stdint.h>

void PinRainfall_Init();
void PinRainfall_SetInterrupt_Callback(void (*callback)(uint32_t tip_ms));

#endif