#ifndef __WATCHDOG_H__
#define __WATCHDOG_H__

#include <stdint.h>

void Watchdog_Init(uint32_t timeout_ms);
void Watchdog_Stop();
void Watchdog_Kick();

#endif