#include <rtos.h>

#include "watchdog.h"

static TX_THREAD tx_monitor;

static void thread_monitor_entry(ULONG);

uint8_t Clock_Init() {
    if(RTOS_CreateThread(&tx_monitor, "Monitor Thread", thread_monitor_entry, 512, 10, 1) != TX_SUCCESS) return -1;

    return 1;
}

static void thread_monitor_entry(ULONG input) {
    Watchdog_Init();

    while(1) {
        Watchdog_Kick();

        tx_thread_sleep(5000);
    }
}

