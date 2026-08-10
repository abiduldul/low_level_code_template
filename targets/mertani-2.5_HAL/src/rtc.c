/**
 ******************************************************************************
 * rtc.c - LSE-backed calendar with Unix timestamp accessors (STM32L4P5)
 *
 * HAL version (converted from the register-level implementation).
 *
 * PREDIV_A = 31, PREDIV_S = 1023 with a 32.768 kHz LSE:
 *   ck_apre = 32768 / 32   = 1024 Hz
 *   ck_spre = 1024  / 1024 = 1 Hz
 *
 * Requirements:
 *   - HAL_RTC_MODULE_ENABLED and HAL_PWR_MODULE_ENABLED
 *   - stm32l4xx_hal_rtc.c, _rtc_ex.c, _pwr.c, _rcc_ex.c in the build
 ******************************************************************************
 */

#include "rtc.h"

#include "stm32l4xx_hal.h"

#include <time.h>
#include <stdint.h>

#define N_PREDIV_S      10
#define PREDIV_S        ((1UL << N_PREDIV_S) - 1UL)         /* 1023 */
#define PREDIV_A        ((1UL << (15 - N_PREDIV_S)) - 1UL)  /* 31   */

/* Magic in backup register 0: lets a warm reset skip re-initialisation so
 * the running calendar is not disturbed. */
#define RTC_BKP_MAGIC   0x32F2U

static RTC_HandleTypeDef hrtc;
static uint8_t RtcInitialized;

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void RTC_Init(void)
{
    RCC_OscInitTypeDef       osc  = {0};
    RCC_PeriphCLKInitTypeDef pclk = {0};

    if (RtcInitialized != 0U) {
        return;
    }

    /* ---- 1. Backup domain access + LSE ---- */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();             /* PWR->CR1 DBP */

    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState       = RCC_LSE_ON;
    osc.PLL.PLLState   = RCC_PLL_NONE;      /* leave the main PLL alone */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return;
    }

    /* ---- 2. Select LSE as the RTC clock and enable the RTC ----
     * If RTCSEL is already set to something else this performs a backup
     * domain reset internally, so it is safe to call unconditionally. */
    pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    pclk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
        return;
    }

    __HAL_RCC_RTC_ENABLE();

    /* ---- 3. Prescalers and calendar format ----
     * HAL_RTC_Init handles the write-protection keys, INIT/INITF entry and
     * exit, and the RSF wait - all the RtcEnterInitMode() plumbing. */
    hrtc.Instance            = RTC;
    hrtc.Init.HourFormat     = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv   = PREDIV_A;
    hrtc.Init.SynchPrediv    = PREDIV_S;
    hrtc.Init.OutPut         = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutRemap    = RTC_OUTPUT_REMAP_NONE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType     = RTC_OUTPUT_TYPE_OPENDRAIN;
    hrtc.Init.OutPutPullUp   = RTC_OUTPUT_PULLUP_NONE;
    if (HAL_RTC_Init(&hrtc) != HAL_OK) {
        return;
    }

    /* ---- 4. Bypass the shadow registers (no RTC_InitTypeDef field for it) */
    __HAL_RTC_WRITEPROTECTION_DISABLE(&hrtc);
    SET_BIT(RTC->CR, RTC_CR_BYPSHAD);
    __HAL_RTC_WRITEPROTECTION_ENABLE(&hrtc);

    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_BKP_MAGIC);

    RtcInitialized = 1U;
}

/* 1 if the calendar was already running before this power cycle, i.e. the
 * time is meaningful and does not need to be set from an external source. */
uint8_t RTC_IsTimeValid(void)
{
    return (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_BKP_MAGIC) ? 1U : 0U;
}

time_t RTC_GetTimestamp(void)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    struct tm       tmp = {0};

    /* GetTime must come first: with BYPSHAD = 0 it locks the shadow
     * registers and GetDate unlocks them. Harmless with BYPSHAD = 1. */
    if (HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) {
        return (time_t)0;
    }
    if (HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) {
        return (time_t)0;
    }

    tmp.tm_hour  = t.Hours;
    tmp.tm_min   = t.Minutes;
    tmp.tm_sec   = t.Seconds;
    tmp.tm_mday  = d.Date;
    tmp.tm_mon   = d.Month - 1;         /* RTC 1..12 -> tm 0..11  */
    tmp.tm_year  = d.Year + 100;        /* RTC 0..99 -> years from 1900 */
    tmp.tm_isdst = 0;

    return mktime(&tmp);
}

void RTC_SetTimestamp(time_t timestamp)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    struct tm      *p_time = gmtime(&timestamp);

    if (p_time == NULL) {
        return;
    }

    t.Hours          = (uint8_t)p_time->tm_hour;
    t.Minutes        = (uint8_t)p_time->tm_min;
    t.Seconds        = (uint8_t)p_time->tm_sec;
    t.TimeFormat     = RTC_HOURFORMAT12_AM;     /* ignored in 24-hour mode */
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;

    d.Date  = (uint8_t)p_time->tm_mday;
    d.Month = (uint8_t)(p_time->tm_mon + 1);
    d.Year  = (uint8_t)(p_time->tm_year - 100);

    /* tm_wday is 0 = Sunday; the RTC uses 1 = Monday .. 7 = Sunday. The old
     * code hard-coded Monday, so the weekday field was always wrong. */
    d.WeekDay = (p_time->tm_wday == 0)
              ? RTC_WEEKDAY_SUNDAY
              : (uint8_t)p_time->tm_wday;

    HAL_PWR_EnableBkUpAccess();

    if (HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) {
        return;
    }
    if (HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) {
        return;
    }

    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_BKP_MAGIC);
}