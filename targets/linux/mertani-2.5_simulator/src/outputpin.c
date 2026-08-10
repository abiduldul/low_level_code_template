#include "main.h"

void OutputPin_Config() {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;

    GPIOC->MODER &= ~(GPIO_MODER_MODE6 | GPIO_MODER_MODE7);
    GPIOC->MODER |= (GPIO_MODER_MODE6_0 | GPIO_MODER_MODE7_0);

    GPIOC->BSRR |= (GPIO_BSRR_BS6 | GPIO_BSRR_BS7);

    (void)RCC->AHB2ENR;

    return;
}