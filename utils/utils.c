#include "utils.h"

uint16_t swap16(const uint16_t src) {
    uint16_t tmp;

    *((uint8_t*)&tmp) = (uint8_t)(src >> 8);
    *((uint8_t*)&tmp + 1) = (uint8_t)src;

    return tmp;
}