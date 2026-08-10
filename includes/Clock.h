#ifndef __CLOCK_H__
#define __CLOCK_H__

#include <stdint.h>
#include <time.h>

uint8_t Clock_Init();
time_t Clock_GetTimestamp();

#endif