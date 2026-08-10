/**
 ******************************************************************************
 * battery.c - Battery / internal temperature measurement for STM32L4P5
 *
 * HAL version (converted from the register-level implementation).
 *
 * Channels (ADC1, regular sequence of 3, software-triggered, scan, single):
 *   Rank 1: IN3  = PC2 (battery divider, 3/5 ratio -> x 5/3 in software)
 *   Rank 2: IN17 = internal temperature sensor
 *   Rank 3: IN0  = VREFINT
 *
 * Requirements:
 *   - HAL_ADC_MODULE_ENABLED in stm32l4xx_hal_conf.h
 *   - stm32l4xx_hal_adc.c and stm32l4xx_hal_adc_ex.c added to the build
 *   - HAL_Init() called and SysTick running (HAL_GetTick / HAL_Delay)
 *   - AHB prescaler = 1 (required for ADC_CLOCK_SYNC_PCLK_DIV1)
 ******************************************************************************
 */

#include "battery.h"

#include "stm32l4xx_hal.h"      /* pulls in stm32l4xx_ll_adc.h for the
                                   __LL_ADC_CALC_* calibration macros */
#include "system.h"

#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */

#define BATTERY_SENSOR_MAX_BATTERY_VOLTAGE      4.0f
#define BATTERY_SENSOR_MIN_BATTERY_VOLTAGE      3.1f
#define BATTERY_SENSOR_EMPTY_BATTERY_VOLTAGE    2.9f

#define BATTERY_DIVIDER_RATIO                   (5.0f / 3.0f)
#define BATTERY_AVG_SAMPLES                     10U

#define BATTERY_ADC_CHANNEL                     ADC_CHANNEL_3   /* PC2 */
#define BATTERY_ADC_GPIO_PORT                   GPIOC
#define BATTERY_ADC_GPIO_PIN                    GPIO_PIN_2

#define BATTERY_POLL_TIMEOUT_MS                 10U

/* Temperature-sensor stabilization time (STM32L4: 120 us typ). */
#ifndef LL_ADC_DELAY_TEMPSENSOR_STAB_US
  #define LL_ADC_DELAY_TEMPSENSOR_STAB_US       120U
#endif

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

static ADC_HandleTypeDef hadc1;

static uint16_t raw_batt, raw_temp, raw_vref;
static float    batt_pin_voltage;
static float    temperature;
static uint8_t  sampling_error;     /* 1 = last Battery_Sampling() failed */

/* ------------------------------------------------------------------------- */
/* MSP - low-level init (clocks + GPIO)                                      */
/*                                                                           */
/* NOTE: if your project already has HAL_ADC_MspInit() in stm32l4xx_hal_msp.c */
/* (CubeMX generates one), DELETE the two functions below and move their     */
/* bodies into that file instead - otherwise you get a duplicate symbol.     */
/* ------------------------------------------------------------------------- */

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) {
        return;
    }

    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin  = BATTERY_ADC_GPIO_PIN;
    gpio.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;   /* analog, ADC-controlled */
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BATTERY_ADC_GPIO_PORT, &gpio);
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) {
        return;
    }

    __HAL_RCC_ADC_CLK_DISABLE();
    HAL_GPIO_DeInit(BATTERY_ADC_GPIO_PORT, BATTERY_ADC_GPIO_PIN);
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void Battery_Init(void)
{
    ADC_ChannelConfTypeDef ch = {0};

    /* ---------------- Handle configuration ---------------- */
    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV1; /* = CKMODE 01 */
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode          = ADC_SCAN_ENABLE;          /* 3 ranks   */
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;      /* per-rank  */
    hadc1.Init.LowPowerAutoWait      = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;                  /* CONT = 0  */
    hadc1.Init.NbrOfConversion       = 3;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.OversamplingMode      = DISABLE;

    /* Calls HAL_ADC_MspInit(), exits deep-power-down, enables the internal
     * regulator and waits its start-up time. */
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        sampling_error = 1U;
        return;
    }

    /* ---------------- Calibration (ADC must be disabled) ---------------- */
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) {
        sampling_error = 1U;
        return;
    }

    /* ---------------- Rank 1: battery divider on PC2 ---------------- */
    ch.Channel      = BATTERY_ADC_CHANNEL;
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
    ch.SingleDiff   = ADC_SINGLE_ENDED;
    ch.OffsetNumber = ADC_OFFSET_NONE;
    ch.Offset       = 0;
    if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK) {
        sampling_error = 1U;
        return;
    }

    /* ---------------- Rank 2: internal temperature sensor ----------------
     * HAL sets TSEN in ADCx_COMMON->CCR automatically here, but it does NOT
     * wait for the sensor to stabilize - that is on us. */
    ch.Channel      = ADC_CHANNEL_TEMPSENSOR;
    ch.Rank         = ADC_REGULAR_RANK_2;
    ch.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;   /* >= 5 us at 80 MHz */
    if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK) {
        sampling_error = 1U;
        return;
    }
    HAL_Delay(1);       /* >= LL_ADC_DELAY_TEMPSENSOR_STAB_US (120 us) */

    /* ---------------- Rank 3: VREFINT (HAL sets VREFEN) ---------------- */
    ch.Channel      = ADC_CHANNEL_VREFINT;
    ch.Rank         = ADC_REGULAR_RANK_3;
    ch.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;   /* >= 4 us at 80 MHz */
    if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK) {
        sampling_error = 1U;
        return;
    }

    sampling_error = 0U;

    Battery_Sampling();
}

void Battery_Sampling(void)
{
    uint32_t sum_batt = 0U, sum_temp = 0U, sum_vref = 0U;
    uint32_t good = 0U;

    for (uint8_t i = 0U; i < BATTERY_AVG_SAMPLES; i++) {
        uint16_t s[3];
        uint8_t  ok = 1U;

        if (HAL_ADC_Start(&hadc1) != HAL_OK) {
            ok = 0U;
        } else {
            for (uint8_t c = 0U; c < 3U; c++) {
                /* With EOCSelection = ADC_EOC_SINGLE_CONV this returns once
                 * per rank, and reading DR clears EOC. */
                if (HAL_ADC_PollForConversion(&hadc1,
                                              BATTERY_POLL_TIMEOUT_MS) != HAL_OK) {
                    ok = 0U;
                    break;
                }
                s[c] = (uint16_t)HAL_ADC_GetValue(&hadc1);
            }
        }

        /* Always stop: on success it just clears state, on failure it aborts
         * a stuck sequence so the next attempt starts clean. */
        (void)HAL_ADC_Stop(&hadc1);

        if (ok) {
            sum_batt += s[0];
            sum_temp += s[1];
            sum_vref += s[2];
            good++;
        }

        System_DelayMs(1);
    }

    /* If nothing converted, keep previous values instead of writing zeros
     * (a zero raw_vref would also divide by zero in the VDDA formula). */
    if (good == 0U) {
        sampling_error = 1U;
        return;
    }

    raw_batt = (uint16_t)(sum_batt / good);
    raw_temp = (uint16_t)(sum_temp / good);
    raw_vref = (uint16_t)(sum_vref / good);

    if (raw_vref == 0U) {
        sampling_error = 1U;
        return;
    }

    /* Actual VDDA in mV from the factory-calibrated VREFINT reading */
    uint32_t vdda_mv =
        __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw_vref, LL_ADC_RESOLUTION_12B);

    /* Battery pin voltage in volts, ratiometric to the real VDDA */
    batt_pin_voltage =
        (float)__LL_ADC_CALC_DATA_TO_VOLTAGE(vdda_mv, raw_batt,
                                             LL_ADC_RESOLUTION_12B) / 1000.0f;

    /* Die temperature in degC from the factory calibration points */
    temperature =
        (float)__LL_ADC_CALC_TEMPERATURE(vdda_mv, raw_temp,
                                         LL_ADC_RESOLUTION_12B);

    sampling_error = 0U;
}

uint8_t Battery_GetPercentage(void)
{
    float batt_voltage = batt_pin_voltage * BATTERY_DIVIDER_RATIO;

    if (batt_voltage <= BATTERY_SENSOR_MIN_BATTERY_VOLTAGE) {
        return 0U;
    }
    if (batt_voltage >= BATTERY_SENSOR_MAX_BATTERY_VOLTAGE) {   /* was '<=': bug */
        return 100U;
    }

    float percent = ((batt_voltage - BATTERY_SENSOR_MIN_BATTERY_VOLTAGE) /
                     (BATTERY_SENSOR_MAX_BATTERY_VOLTAGE -
                      BATTERY_SENSOR_MIN_BATTERY_VOLTAGE)) * 100.0f;

    return (uint8_t)(percent + 0.5f);
}

float Battery_GetTemperature(void)
{
    return temperature;
}

/* 1 = last sampling attempt failed (values are stale). */
uint8_t Battery_HasError(void)
{
    return sampling_error;
}