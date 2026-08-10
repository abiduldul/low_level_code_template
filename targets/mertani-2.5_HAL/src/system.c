#include "system.h"
#include "stm32l4xx_hal.h"
#include "battery.h"
#include "watchdog.h"
#include "rtc.h"

extern void led_init();

#define PCLK    80000000U

static volatile uint32_t sys_tick_ms;
static TIM_HandleTypeDef htim6;
UART_HandleTypeDef hlpuart1;

static void clock_init(void);
static void lpuart1_init(void);
static void timer6_init(void);

static void startup_error_trap(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    (void)RCC->AHB2ENR;

    GPIOA->MODER &= ~GPIO_MODER_MODE8;
    GPIOA->MODER |= GPIO_MODER_MODE8_0;           

    for (;;) {
        GPIOA->ODR ^= GPIO_ODR_OD8;
        for (volatile uint32_t i = 0U; i < 400000U; i++) { __NOP(); }
    }
}

void System_Init(void)
{
    /* FPU on (CP10/CP11 full access) - required for -mfloat-abi=hard.
     * Usually done in SystemInit()/startup, kept here as a safety net. */
    SCB->CPACR |= ((3UL << (10U * 2U)) | (3UL << (11U * 2U)));

    if (HAL_Init() != HAL_OK) {
        startup_error_trap();
    }

    timer6_init();

    clock_init();

    /* Peripheral power rails / wireless enable: PC6, PC7 high */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
    GPIO_InitStruct.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    lpuart1_init();
    led_init();

    Watchdog_Init(30);
    RTC_Init();
    Battery_Init();
}

static void lpuart1_init(void)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection   = RCC_PERIPHCLK_LPUART1;
    PeriphClkInit.Lpuart1ClockSelection  = RCC_LPUART1CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        startup_error_trap();
    }

    hlpuart1.Instance                    = LPUART1;
    hlpuart1.Init.BaudRate               = 115200U;
    hlpuart1.Init.WordLength             = UART_WORDLENGTH_8B;
    hlpuart1.Init.StopBits               = UART_STOPBITS_1;
    hlpuart1.Init.Parity                 = UART_PARITY_NONE;
    hlpuart1.Init.Mode                   = UART_MODE_TX_RX;
    hlpuart1.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    hlpuart1.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&hlpuart1) != HAL_OK) {
        startup_error_trap();
    }
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (huart->Instance == LPUART1) {
        __HAL_RCC_LPUART1_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();

        /* PC1 = LPUART1_TX, PC0 = LPUART1_RX (AF8) - board-verified */
        GPIO_InitStruct.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF8_LPUART1;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance == LPUART1) {
        __HAL_RCC_LPUART1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_0 | GPIO_PIN_1);
    }
}

static void timer6_init(void)
{
    __HAL_RCC_TIM6_CLK_ENABLE();

    htim6.Instance               = TIM6;
    htim6.Init.Prescaler         = (PCLK / 1000000U) - 1U; /* 80 MHz -> 1 MHz */
    htim6.Init.Period            = 1000U - 1U;             /* 1 MHz -> 1 ms   */
    htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim6) != HAL_OK) {
        startup_error_trap();
    }

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 15, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

    if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK) {   /* THE missing piece  */
        startup_error_trap();
    }
}

static void clock_init(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

   __HAL_RCC_PWR_CLK_ENABLE();

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1)
            != HAL_OK) {
        startup_error_trap();
    }

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE |
                                       RCC_OSCILLATORTYPE_HSI48;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSI48State     = RCC_HSI48_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 1;
    RCC_OscInitStruct.PLL.PLLN       = 20;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ       = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR       = RCC_PLLR_DIV2;  /* 8/1*20/2 = 80 MHz */

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        startup_error_trap();      /* HSE dead / PLL no lock - LED blinks  */
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK  |
                                       RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 |
                                       RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        startup_error_trap();
    }
}

uint32_t System_GetClockFreq(void)
{
    return SystemCoreClock;        /* kept accurate by HAL_RCC_ClockConfig */
}

void System_DelayMs(uint32_t ms)
{
    uint32_t tmp = System_GetTickMs();
    while ((System_GetTickMs() - tmp) < ms) { }
}

uint32_t System_GetTickMs(void)
{
    return sys_tick_ms;
}

void TIM6_DAC_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&htim6, TIM_FLAG_UPDATE) != RESET) {
        if (__HAL_TIM_GET_IT_SOURCE(&htim6, TIM_IT_UPDATE) != RESET) {
            __HAL_TIM_CLEAR_IT(&htim6, TIM_IT_UPDATE);
            sys_tick_ms++;
        }
    }
}

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

uint32_t HAL_GetTick(void)
{
    return System_GetTickMs();
}

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    (void)TickPriority;
    return HAL_OK;    
}