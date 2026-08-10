/**
 ******************************************************************************
 * watchdog.c - Independent watchdog (IWDG) on STM32L4P5
 *
 * HAL version (converted from the register-level implementation).
 *
 * Requirements:
 *   - HAL_IWDG_MODULE_ENABLED in stm32l4xx_hal_conf.h
 *   - stm32l4xx_hal_iwdg.c in the build
 *   - HAL_Init() called and SysTick running: HAL_IWDG_Init() times out on
 *     the PVU/RVU/WVU wait using HAL_GetTick().
 ******************************************************************************
 */

#include "watchdog.h"

#include "stm32l4xx_hal.h"

/* Nominal LSI frequency. The LSI is an RC oscillator with a wide tolerance
 * (roughly -5% / +10% over temperature on the L4), so treat every timeout
 * below as approximate and kick well before it expires. */
#define WATCHDOG_LSI_HZ             32000UL

#define WATCHDOG_MAX_RELOAD         4095UL
#define WATCHDOG_MAX_TIMEOUT_S      32UL    /* 4096 * 256 / 32000 = 32.7 s */

static IWDG_HandleTypeDef hiwdg;

void Watchdog_Init(uint32_t timeout_s)
{
    static const struct {
        uint32_t divider;
        uint32_t hal_prescaler;
    } presc[] = {
        {   4UL, IWDG_PRESCALER_4   },
        {   8UL, IWDG_PRESCALER_8   },
        {  16UL, IWDG_PRESCALER_16  },
        {  32UL, IWDG_PRESCALER_32  },
        {  64UL, IWDG_PRESCALER_64  },
        { 128UL, IWDG_PRESCALER_128 },
        { 256UL, IWDG_PRESCALER_256 },
    };

    uint32_t chosen_prescaler = IWDG_PRESCALER_256;
    uint32_t reload           = WATCHDOG_MAX_RELOAD;

    if (timeout_s == 0UL) {
        timeout_s = 1UL;
    }
    if (timeout_s > WATCHDOG_MAX_TIMEOUT_S) {
        timeout_s = WATCHDOG_MAX_TIMEOUT_S;
    }

    /* Smallest prescaler whose reload still fits in 12 bits gives the best
     * timing resolution for the requested timeout. */
    for (uint32_t i = 0U; i < (sizeof(presc) / sizeof(presc[0])); i++) {
        uint32_t ticks = (WATCHDOG_LSI_HZ / presc[i].divider) * timeout_s;

        if (ticks <= WATCHDOG_MAX_RELOAD) {
            chosen_prescaler = presc[i].hal_prescaler;
            reload           = (ticks > 0UL) ? (ticks - 1UL) : 0UL;
            break;
        }
    }

    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = chosen_prescaler;
    hiwdg.Init.Reload    = reload;
    hiwdg.Init.Window    = IWDG_WINDOW_DISABLE;     /* = 0x0FFF */

    /* HAL_IWDG_Init writes the start key (0xCCCC), the access key (0x5555),
     * PR and RLR, waits for PVU/RVU/WVU to clear, then either programs WINR
     * or issues a refresh - the same branch the register version had. */
    (void)HAL_IWDG_Init(&hiwdg);

    /* Halt the counter when the core is stopped by the debugger. */
    __HAL_DBGMCU_FREEZE_IWDG();
}

/* The IWDG cannot be disabled once started - only a system reset clears it.
 * Kept for API compatibility; it was already a no-op in the register version. */
void Watchdog_Stop(void)
{
}

void Watchdog_Kick(void)
{
    (void)HAL_IWDG_Refresh(&hiwdg);     /* writes KR = 0xAAAA */
}