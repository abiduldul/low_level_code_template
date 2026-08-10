#include "main.h"

void Init() {
    Clock_Config();
    SysTick_Config(80000000/1000);
    OutputPin_Config();
    LPUART1_Config();
    USART1_Config();
    USART2_Config();
    USART3_Config();
    UART4_Config();

    return;
}

void SystemClock_Config_80MHz(void) {
    // 1. Ensure PWR clock is enabled and VOS is Range 1 (Required for >26MHz)
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    PWR->CR1 &= ~PWR_CR1_VOS;
    PWR->CR1 |= (1 << PWR_CR1_VOS_Pos); // Range 1 
    while ((PWR->SR2 & PWR_SR2_VOSF));  // Wait for voltage scaling to settle

    // 2. Enable HSE (8 MHz)
    // Use RCC_CR_HSEBYP here as well if your 8MHz is a square wave from an ST-Link MCO
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    // 3. Configure Flash Latency to 4 Wait States
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    FLASH->ACR |= FLASH_ACR_LATENCY_4WS;
    // L4+ CRITICAL: Must wait for the flash controller to acknowledge the wait states
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_4WS); 
    
    // 4. Configure the Main PLL (8MHz input -> 80MHz output)
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY);
    
    // Note: In L4P5, PLLM is bits 7:4. 0000 = Division by 1.
    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSE | 
                   (20 << RCC_PLLCFGR_PLLN_Pos) | 
                   RCC_PLLCFGR_PLLREN;            
                   
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    // 5. CRITICAL: Clear all APB/AHB Prescalers to force division by 1
    // This kills the 40MHz "Half-Baud" APB1 trap.
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);

    // 6. Set SYSCLK source to PLL
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}