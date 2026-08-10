#include "led.h"

static uint8_t ledStatus = 0;

void LED_On() {
    ledStatus = 1;

    return;
}

void LED_Off() {
    ledStatus = 0;

    return;
}

uint8_t LED_GetStatus() {
    return ledStatus;
}