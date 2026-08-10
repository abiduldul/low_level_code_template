#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>

#define map(x, in_min, in_max, out_min, out_max) ((x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min)
#define constrain(x, min, max) ((x < min) ? min : (x > max) ? max : x)
#define constrain_max(x, max) ((x > max) ? max : x)
#define constrain_min(x, min) ((x < min) ? min : x)

uint16_t swap16(const uint16_t src);

#endif