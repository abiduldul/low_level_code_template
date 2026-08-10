#include "pinrainfall.h"

#include <stdlib.h>
#include <time.h>
#include <stdio.h>

static void (*__callback)() = NULL;

void PinRainfall_Init() {
    return;
}

void PinRainfall_SetInterrupt_Callback(void (*callback)(uint32_t tip_ms)) {
    __callback = callback;
}

static void interrupt_cb() {
    struct timespec ts;

    if(clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        if(__callback != NULL) {
            __callback(ts.tv_nsec / 1000);
        }
    }

    return;
}