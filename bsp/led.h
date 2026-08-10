#ifndef LED_H
#define LED_H

#include <stdint.h>

void led_init(void);
uint8_t LED_GetStatus(void);
void LED_On(void);
void LED_Off(void);

#endif /* LED_H */