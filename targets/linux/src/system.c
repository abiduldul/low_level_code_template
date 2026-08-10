#include "system.h"
#include "nvm.h"
#include "rs485.h"
#include "pinrainfall.h"
#include "debugger.h"
#include "rtc.h"
#include "watchdog.h"
#include "battery.h"

#include <unistd.h>

void System_Init() {
    NVM_Init();
    PinRainfall_Init();
    Debugger_Init();
    RTC_Init();
    Watchdog_Init(30);

    Battery_Init();
    
    return;
}

void System_DelayMs(uint32_t ms) {
    usleep(ms * 1000);

    return;
}

uint32_t System_GetTickMs() {
    return 0;
}