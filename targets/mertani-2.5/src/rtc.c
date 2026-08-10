#include "rtc.h"

#include "stm32l4p5xx.h"

#define N_PREDIV_S 10

/* Synchronous prediv  → 2^10 - 1 = 1023 */
#define PREDIV_S ((1UL << N_PREDIV_S) - 1UL)

/* Asynchronous prediv → 2^(15-10) - 1 = 31  (gives 32 kHz / 32 / 1024 ≈ 1 Hz) */
#define PREDIV_A ((1UL << (15 - N_PREDIV_S)) - 1UL)

static uint8_t RtcInitialized;

/* ==========================================================================
 * BCD helpers
 * ========================================================================== */
static inline uint8_t BCD2BIN( uint8_t bcd ) {
    return (uint8_t)( ( ( bcd >> 4 ) * 10u ) + ( bcd & 0x0Fu ) );
}
static inline uint8_t BIN2BCD( uint8_t bin ) {
    return (uint8_t)( ( ( bin / 10u ) << 4 ) | ( bin % 10u ) );
}

static time_t unixtimestamp_get(void);
static void RtcGetDate(uint8_t *day, uint8_t *month, uint8_t *year);
static void RtcGetTime(uint8_t *hours, uint8_t *minutes, uint8_t *seconds);
static void RtcSetCalendar(uint8_t day, uint8_t month, uint8_t year,
                            uint8_t hours, uint8_t minutes, uint8_t seconds);
static void RtcEnterInitMode(void);
static void RtcExitInitMode(void);
static void RtcWriteProtectionEnable(void);
static void RtcWriteProtectionDisable(void);

void RTC_Init() {
    if (RtcInitialized == 0) {
        /* ---- 1. Enable LSE and select it as RTC clock ---- */

        /* Enable power interface clock so we can write to RCC->BDCR */
        RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
        __DSB();

        /* Unlock backup domain */
        PWR->CR1 |= PWR_CR1_DBP;
        while ((PWR->CR1 & PWR_CR1_DBP) == 0U)
            ;

        /* Enable LSE (if not already running) */
        if ((RCC->BDCR & RCC_BDCR_LSERDY) == 0U) {
            RCC->BDCR |= RCC_BDCR_LSEON;
            while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0U);
        }

        /* Select LSE as RTC clock and enable RTC */
        RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL_Msk) | (0x01UL << RCC_BDCR_RTCSEL_Pos);
        RCC->BDCR |= RCC_BDCR_RTCEN;

        /* ---- 2. Configure RTC prescalers and calendar ---- */
        RtcWriteProtectionDisable();
        RtcEnterInitMode();

        /*
         * RTC_PRER: PREDIV_A in bits [22:16], PREDIV_S in bits [14:0]
         * With PREDIV_A = 31, PREDIV_S = 1023 and a 32.768 kHz LSE:
         *   f_ck_apre = 32768 / (31+1)   = 1024 Hz
         *   f_ck_spre = 1024  / (1023+1) = 1 Hz     (calendar second)
         *   Sub-second resolution = 1/1024 s ≈ 0.977 ms
         */
        RTC->PRER = (PREDIV_A << RTC_PRER_PREDIV_A_Pos) |
                    (PREDIV_S << RTC_PRER_PREDIV_S_Pos);

        /* 24-hour format, no output */
        RTC->CR &= ~RTC_CR_FMT; /* 0 = 24-hour */

        /* Enable bypass shadow registers so we always read live values */
        RTC->CR |= RTC_CR_BYPSHAD;

        RtcExitInitMode();
        RtcWriteProtectionEnable();

        RtcInitialized = 1;
    }
}

time_t RTC_GetTimestamp() {
    return unixtimestamp_get();
}

void RTC_SetTimestamp(time_t timestamp) {
    struct tm* p_time;

    p_time = gmtime(&timestamp);

    RtcSetCalendar(p_time->tm_mday, p_time->tm_mon + 1, p_time->tm_year - 100, p_time->tm_hour,
    p_time->tm_min, p_time->tm_sec);

    return;
}

/*!
 * \brief Disable RTC write protection.
 *        Must be called before modifying any RTC register.
 */
static void RtcWriteProtectionDisable(void) {
    RTC->WPR = 0xCAU;
    RTC->WPR = 0x53U;

    return;
}

/*!
 * \brief Re-enable RTC write protection.
 */
static void RtcWriteProtectionEnable(void) {
    RTC->WPR = 0xFFU; /* Any wrong key re-activates protection */

    return;
}

/*!
 * \brief Enter RTC initialisation mode (required to write TR/DR/PRER).
 */
static void RtcEnterInitMode(void) {
    RTC->ICSR |= RTC_ICSR_INIT; 
    while (!(RTC->ICSR & RTC_ICSR_INITF));
}

/*!
 * \brief Exit RTC initialisation mode.
 */
static void RtcExitInitMode(void) {
    RTC->ICSR &= ~RTC_ICSR_INIT;
    /* If BYPSHAD=0 the calendar is not accessible until RSF=1.
     * With BYPSHAD=1 (set above) this wait is not strictly required,
     * but we keep it for safety.                                      */
}

static void RtcSetCalendar(uint8_t day, uint8_t month, uint8_t year,
                            uint8_t hours, uint8_t minutes, uint8_t seconds) {
    day = BIN2BCD(day);
    month = BIN2BCD(month);
    year = BIN2BCD(year);
    hours = BIN2BCD(hours);
    minutes = BIN2BCD(minutes);
    seconds = BIN2BCD(seconds);

    uint32_t tr = ((uint32_t)(1) << RTC_TR_PM_Pos) | ((uint32_t)((hours >> 4) & 0x03U) << RTC_TR_HT_Pos) | ((uint32_t)(hours & 0x0FU) << RTC_TR_HU_Pos) | ((uint32_t)((minutes >> 4) & 0x07U) << RTC_TR_MNT_Pos) | ((uint32_t)(minutes & 0x0FU) << RTC_TR_MNU_Pos) | ((uint32_t)((seconds >> 4) & 0x07U) << RTC_TR_ST_Pos) | ((uint32_t)(seconds & 0x0FU) << RTC_TR_SU_Pos);

    uint32_t dr = ((uint32_t)((year >> 4) & 0x0FU) << RTC_DR_YT_Pos) | ((uint32_t)(year & 0x0FU) << RTC_DR_YU_Pos) | ((uint32_t)(1 & 0x07U) << RTC_DR_WDU_Pos) | ((uint32_t)((month >> 4) & 0x01U) << RTC_DR_MT_Pos) | ((uint32_t)(month & 0x0FU) << RTC_DR_MU_Pos) | ((uint32_t)((day >> 4) & 0x03U) << RTC_DR_DT_Pos) | ((uint32_t)(day & 0x0FU) << RTC_DR_DU_Pos);

    PWR->CR1 |= PWR_CR1_DBP;

    RtcWriteProtectionDisable();
    RtcEnterInitMode();

    RTC->TR = tr;
    RTC->DR = dr;

    RtcExitInitMode();
    RtcWriteProtectionEnable();
}

static void RtcGetTime(uint8_t *hours, uint8_t *minutes, uint8_t *seconds) {
    uint32_t tr1, tr2;
    do {
        tr1 = RTC->TR;
        tr2 = RTC->TR;
    } while (tr1 != tr2);
    uint32_t tr = tr2;
    *hours = (uint8_t)(((tr & RTC_TR_HT_Msk) >> RTC_TR_HT_Pos) << 4) | (uint8_t)((tr & RTC_TR_HU_Msk) >> RTC_TR_HU_Pos);
    *minutes = (uint8_t)(((tr & RTC_TR_MNT_Msk) >> RTC_TR_MNT_Pos) << 4) | (uint8_t)((tr & RTC_TR_MNU_Msk) >> RTC_TR_MNU_Pos);
    *seconds = (uint8_t)(((tr & RTC_TR_ST_Msk) >> RTC_TR_ST_Pos) << 4) | (uint8_t)((tr & RTC_TR_SU_Msk) >> RTC_TR_SU_Pos);

    return;
}

static void RtcGetDate(uint8_t *day, uint8_t *month, uint8_t *year) {
    uint32_t dr = RTC->DR;
    *year = (uint8_t)(((dr & RTC_DR_YT_Msk) >> RTC_DR_YT_Pos) << 4) | (uint8_t)((dr & RTC_DR_YU_Msk) >> RTC_DR_YU_Pos);
    *month = (uint8_t)(((dr & RTC_DR_MT_Msk) >> RTC_DR_MT_Pos) << 4) | (uint8_t)((dr & RTC_DR_MU_Msk) >> RTC_DR_MU_Pos);
    *day = (uint8_t)(((dr & RTC_DR_DT_Msk) >> RTC_DR_DT_Pos) << 4) | (uint8_t)((dr & RTC_DR_DU_Msk) >> RTC_DR_DU_Pos);

    return;
}

static time_t unixtimestamp_get(void) {
    uint8_t day, month, year, hours, minutes, seconds;

    RtcGetTime(&hours, &minutes, &seconds);
    RtcGetDate(&day, &month, &year);

    hours = BCD2BIN(hours);
    minutes = BCD2BIN(minutes);
    seconds = BCD2BIN(seconds);

    day = BCD2BIN(day);
    month = BCD2BIN(month);
    year = BCD2BIN(year);

    struct tm tmp_tm = {0};

    tmp_tm.tm_hour = hours;
    tmp_tm.tm_min = minutes;
    tmp_tm.tm_sec = seconds;
    tmp_tm.tm_mday = day;
    tmp_tm.tm_mon = month - 1;
    tmp_tm.tm_year = year + 100;
    tmp_tm.tm_isdst = 0;
    
    return mktime(&tmp_tm);
}