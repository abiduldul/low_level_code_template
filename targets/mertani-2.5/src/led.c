#include "led.h"
#include "stm32l4p5xx.h"

static uint8_t ledStatus;

void led_init() {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    volatile uint32_t dummy = RCC->AHB2ENR;
    (void)dummy;

    GPIOA->MODER &= ~GPIO_MODER_MODE8_Msk;
    GPIOA->MODER |= GPIO_MODER_MODE8_0;

    GPIOA->OTYPER &= ~GPIO_OTYPER_OT8;

    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED8;

    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD8;

    return;
}

uint8_t LED_GetStatus() {
    return ledStatus;
}

void LED_On() {
    GPIOA->BSRR = GPIO_BSRR_BS8;
    ledStatus = 1;

    return;
}

void LED_Off() {
    GPIOA->BSRR = GPIO_BSRR_BR8;
    ledStatus = 0;

    return;
}