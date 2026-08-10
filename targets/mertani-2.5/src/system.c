#include "system.h"
#include "stm32l4xx_hal.h"
#include "stm32l4p5xx.h"
#include "battery.h"
#include "watchdog.h"

#include "rtc.h"

extern void led_init();

#define PCLK    80000000

static volatile uint32_t sys_tick_ms;

static void lpuart1_init() {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    GPIOC->MODER &= ~(GPIO_MODER_MODE0 | GPIO_MODER_MODE1);
    GPIOC->MODER |= (GPIO_MODER_MODE0_1 | GPIO_MODER_MODE1_1);

    GPIOC->AFR[0] &= ~(GPIO_AFRL_AFSEL0 | GPIO_AFRL_AFSEL1);
    GPIOC->AFR[0] |= ((8UL << GPIO_AFRL_AFSEL0_Pos) |
                      (8UL << GPIO_AFRL_AFSEL1_Pos)); // AF8

    RCC->APB1ENR2 |= RCC_APB1ENR2_LPUART1EN;

    LPUART1->BRR = (uint32_t)((256ULL * PCLK) / 115200);

    LPUART1->CR1 &= ~(USART_CR1_M1 | USART_CR1_M0 | USART_CR1_PCE);
    LPUART1->CR1 |= (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE);

    return;
}

static void timer6_init() {
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;

    RCC->APB1RSTR1 |= RCC_APB1RSTR1_TIM6RST;
    RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_TIM6RST;

    TIM6->PSC = (PCLK / 1000000) - 1;

    TIM6->ARR = 1000 - 1;

    TIM6->DIER |= TIM_DIER_UIE;

    NVIC_SetPriority(TIM6_DAC_IRQn, 15);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);

    TIM6->CR1 |= TIM_CR1_CEN;

    return;
}

static void clock_init(void) {
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

    // 7.
    RCC->CRRCR |= RCC_CRRCR_HSI48ON;
    uint32_t t0 = 0;
    while (!(RCC->CRRCR & RCC_CRRCR_HSI48RDY)) {
        if (++t0 > 1000000U) break;   /* jangan menggantung selamanya */
    }


}

void System_Init() {
    // Enable FPU for CP10 and CP11
    SCB->CPACR |= ((3UL << 10*2) | (3UL << 11*2));
    clock_init();

    if(SysTick_Config(PCLK / 1000) != 0) {
        while(1);
    }

    (void)RCC->AHB2ENR;

    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;

    GPIOC->MODER &= ~(GPIO_MODER_MODE6 | GPIO_MODER_MODE7);
    GPIOC->MODER |= (GPIO_MODER_MODE6_0 | GPIO_MODER_MODE7_0);

    GPIOC->BSRR |= (GPIO_BSRR_BS6 | GPIO_BSRR_BS7);

    timer6_init();
    lpuart1_init();
    led_init();

    Watchdog_Init(30);

    RTC_Init();

    Battery_Init();

    HAL_Init();

    return;
}

uint32_t System_GetClockFreq() {
    return PCLK;
}

void System_DelayMs(uint32_t ms) {
    uint32_t tmp = System_GetTickMs();

    while(System_GetTickMs() - tmp < ms);

    return;
}

uint32_t System_GetTickMs() {
    return sys_tick_ms;
}

void TIM6_DAC_IRQHandler() {
    if(TIM6->SR & TIM_SR_UIF) {
        TIM6->SR = ~TIM_SR_UIF;

        sys_tick_ms++;
    }
}

int __io_putchar(int ch) {
    while(!(LPUART1->ISR & USART_ISR_TXE_TXFNF));

    LPUART1->TDR = (char)ch;

    return ch;
}

uint32_t HAL_GetTick(void)
{
    return System_GetTickMs();
}

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    (void)TickPriority;
    return HAL_OK;      /* TIM6 is already running; do not touch SysTick */
}