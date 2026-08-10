#include "watchdog.h"

#include "stm32l4p5xx.h"

void Watchdog_Init(uint32_t timeout_s) {
    IWDG->KR = 0xCCCC;

    IWDG->KR = 0x5555;

    IWDG->PR = (IWDG_PR_PR_2 | IWDG_PR_PR_1);

    IWDG->RLR = 4095;

    while((IWDG->SR & (IWDG_SR_WVU | IWDG_SR_RVU | IWDG_SR_PVU)) != 0x0);

    if(IWDG->WINR != 4095) {
        IWDG->WINR = 4095;
    } else {
        IWDG->KR = 0xAAAA;
    }

    DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP;

    return;
}

void Watchdog_Stop() {
    return;
}

void Watchdog_Kick() {
    IWDG->KR = 0xAAAA;
}