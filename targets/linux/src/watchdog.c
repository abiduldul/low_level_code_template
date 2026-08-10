#include "watchdog.h"

#include "watchdog/watchdog_linux.h"

void Watchdog_Init(uint32_t timeout_s) {
    wdt_configure(timeout_s);
    wdt_start();

    return;
}

void Watchdog_Stop() {
    wdt_stop();

    return;
}

void Watchdog_Kick() {
    wdt_kick();

    return;
}