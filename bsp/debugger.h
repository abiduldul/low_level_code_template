#ifndef __DEBUGGER_H__
#define __DEBUGGER_H__

#include <stdint.h>

void Debugger_Init();
void Debugger_Print(const char* buffer, uint16_t length);
#endif