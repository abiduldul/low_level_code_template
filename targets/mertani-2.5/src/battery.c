#include "battery.h"

#include "stm32l4p5xx.h"

#include "utils.h"

#define VREFINT_CAL_ADDR                        0x1FFF75AA  /* datasheet p. 19 */
#define VREFINT_CAL                             ((uint16_t*) VREFINT_CAL_ADDR)

#define TS_CAL1                                 *((uint16_t*) 0x1FFF75A8) // Temp at 30°C (3.0V)
#define TS_CAL2                                 *((uint16_t*) 0x1FFF75CA) // Temp at 130°C (3.0V)

#define BATTERY_SENSOR_MAX_BATTERY_VOLTAGE      4.0f
#define BATTERY_SENSOR_MIN_BATTERY_VOLTAGE      3.1f
#define BATTERY_SENSOR_EMPTY_BATTERY_VOLTAGE    2.9f

static void adc1_read_raw(uint16_t* out_in3, uint16_t* out_temp, uint16_t* out_vref);
static void adc_calculate_voltage(uint16_t raw_in3, uint16_t raw_temp, uint16_t raw_vref, 
    float* out_voltage, float* out_temp);

static uint32_t batt_pin_raw_avg, temperature_raw_avg, vrefint_raw_avg;
static float batt_pin_voltage, temperature;

void Battery_Init() {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;  // Enable GPIOC clock
    RCC->AHB2ENR |= RCC_AHB2ENR_ADCEN;    // Enable ADC block clock
    
    ADC12_COMMON->CCR &= ~ADC_CCR_CKMODE_Msk;
    ADC12_COMMON->CCR |= (1U << ADC_CCR_CKMODE_Pos); // 01: HCLK/1

    GPIOC->MODER |= GPIO_MODER_MODE2_Msk;    // 11: Set PC2 to Analog mode
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPD2_Msk;   // 00: No pull-up, no pull-down
    //GPIOC->ASCR  |= GPIO_ASCR_ASC2;          // CRITICAL for STM32L4: Connect analog

    ADC1->CR &= ~ADC_CR_DEEPPWD;             // Exit deep power-down mode
    ADC1->CR |= ADC_CR_ADVREGEN;             // Enable ADC internal voltage regulator
    
    // Delay for voltage regulator startup (tADCVREG_SETUP typically ~20us)
    for (volatile uint32_t i = 0; i < 10000; i++);

    ADC1->CR &= ~ADC_CR_ADEN;                // Ensure ADC is disabled before calibration
    ADC1->CR |= ADC_CR_ADCAL;                // Start calibration
    while (ADC1->CR & ADC_CR_ADCAL);         // Wait until calibration hardware clears the bit

    ADC1->ISR |= ADC_ISR_ADRDY;              // Clear the ADRDY flag by writing 1
    ADC1->CR |= ADC_CR_ADEN;                 // Enable the ADC
    while (!(ADC1->ISR & ADC_ISR_ADRDY));    // Wait until ADC is ready

    ADC12_COMMON->CCR |= (ADC_CCR_VREFEN | ADC_CCR_TSEN);

    // Sequence Length: 2 conversions (L = 0001 -> 1+1 = 2)
    ADC1->SQR1 &= ~ADC_SQR1_L_Msk;
    ADC1->SQR1 |= (2U << ADC_SQR1_L_Pos);

    // Sequence 1: Channel 3 (PC2), Sequence 2: Channel 17 (Temperature)
    ADC1->SQR1 &= ~(ADC_SQR1_SQ1_Msk | ADC_SQR1_SQ2_Msk | ADC_SQR1_SQ3_Msk);
    ADC1->SQR1 |= ((3U << ADC_SQR1_SQ1_Pos) | (17U << ADC_SQR1_SQ2_Pos) | (0U << ADC_SQR1_SQ3_Pos));
    
    ADC1->SMPR1 |= ((ADC_SMPR1_SMP0) | (ADC_SMPR1_SMP3)); // Add a generous sampling time for Channel 0 (VREFINT requires >4us)  // Channel 3 is in SMPR1
    ADC1->SMPR2 |= (ADC_SMPR2_SMP17); // Channel 17 is in SMPR2

    for(uint32_t i = 0; i < 100000; i++);

    
    Battery_Sampling();

    return;
}

void Battery_Sampling(void) {
    batt_pin_raw_avg    = 0;
    temperature_raw_avg = 0;
    vrefint_raw_avg     = 0;

    for(uint8_t i = 0; i < 10; i++) {
        uint16_t batt_pin_raw, temperature_raw, vrefint_raw;

        adc1_read_raw(&batt_pin_raw, &temperature_raw, &vrefint_raw);

        batt_pin_raw_avg += batt_pin_raw;
        temperature_raw_avg += temperature_raw;
        vrefint_raw_avg += vrefint_raw;

        for(uint32_t d = 0; d < 1000; d++);
    }


    batt_pin_raw_avg    /= 10;
    temperature_raw_avg /= 10;
    vrefint_raw_avg     /= 10;

    adc_calculate_voltage(batt_pin_raw_avg, temperature_raw_avg,
                          vrefint_raw_avg, &batt_pin_voltage, &temperature);
    return;
}

static void adc1_read_raw(uint16_t* out_in3, uint16_t* out_temp, uint16_t* out_vref) {
    // Start the ADC sequence conversion
    ADC1->CR |= ADC_CR_ADSTART;

    // Wait for the first conversion (IN3)
    while (!(ADC1->ISR & ADC_ISR_EOC));
    *out_in3 = ADC1->DR; // Reading DR clears the EOC flag automatically

    // Wait for the second conversion (IN17 / Temp)
    while (!(ADC1->ISR & ADC_ISR_EOC));
    *out_temp = ADC1->DR;

    while(!(ADC1->ISR & ADC_ISR_EOC));
    *out_vref = ADC1->DR;

    ADC1->ISR |= ADC_ISR_EOS;

    return;
}

static void adc_calculate_voltage(uint16_t raw_in3, uint16_t raw_temp, uint16_t raw_vref, 
    float* out_voltage, float* out_temp) {
    if(raw_vref == 0) return;

    float true_vdda = 3.0f * ((float)(*VREFINT_CAL) / (float)raw_vref);

    *out_voltage = true_vdda * ((float)raw_in3 / 4096.0f);

    float vdda_ratio = true_vdda / 3.0f;
    float normalized_raw_temp = (float)raw_temp * vdda_ratio;

    *out_temp = ((130.0f - 30.0f) / (float)(TS_CAL2 - TS_CAL1)) * (normalized_raw_temp - (float)TS_CAL1) + 30.0f;

    return;
}

uint8_t Battery_GetPercentage() {
    float batt_voltage = batt_pin_voltage * (5.0f / 3.0f);
    
    batt_voltage = constrain(batt_voltage, BATTERY_SENSOR_MIN_BATTERY_VOLTAGE,
                            BATTERY_SENSOR_MAX_BATTERY_VOLTAGE);

    float percent = map(batt_voltage, BATTERY_SENSOR_MIN_BATTERY_VOLTAGE,
                            BATTERY_SENSOR_MAX_BATTERY_VOLTAGE, 0.0f, 100.0f);

    if(percent < 0.0f)   percent = 0.0f;
    if(percent > 100.0f) percent = 100.0f;

    return (uint8_t)(percent + 0.5f);
}

float Battery_GetTemperature() {
    return temperature;
}