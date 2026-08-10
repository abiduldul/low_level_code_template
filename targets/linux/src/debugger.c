#include "debugger.h"

#include <stdio.h>
#include <string.h>

static char transmit_buffer[512];

void Debugger_Init() {
    return;
}

void Debugger_Print(const char* buffer, uint16_t length) {
    memcpy(transmit_buffer, buffer, length);

    printf("%.*s", length, transmit_buffer);

    return;
}