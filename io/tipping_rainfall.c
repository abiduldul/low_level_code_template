#include "sensor.h"

#include "pinrainfall.h"

#define SENSOR_VALUE_RAINFALL       0
#define SENSOR_VALUE_RAINDURATION   1

static void tipping_rainfall_init();
static uint8_t tipping_rainfall_read();

static void rainfallpin_callback(uint32_t tip_ms);
static void tipping_rainfall_config(void);
static void tipping_rainfall_error_handler(void);

static SensorValue_t _values[2];
static uint32_t tips;
static uint32_t rain_duration;
static uint32_t last_tip_ms;

Sensor_t tipping_rainfall = {
    .id            = 0x1040,        /* 0x0000 sebelumnya */
    .init          = tipping_rainfall_init,
    .read          = tipping_rainfall_read,
    .config        = tipping_rainfall_config,        
    .error_handler = tipping_rainfall_error_handler, 
    .nb_values     = 2,
    .values        = _values
};

static void tipping_rainfall_init() {
    PinRainfall_SetInterrupt_Callback(rainfallpin_callback);

    _values[SENSOR_VALUE_RAINFALL].value = (void*)&tips;
    _values[SENSOR_VALUE_RAINFALL].data_type = DATATYPE_UINT32;

    _values[SENSOR_VALUE_RAINDURATION].value = (void*)&rain_duration;
    _values[SENSOR_VALUE_RAINDURATION].data_type = DATATYPE_UINT32;

    last_tip_ms = 0;
    tips = 0;
    rain_duration = 0;

    return;
}

static uint8_t tipping_rainfall_read() {
    return 1;
}

static void rainfallpin_callback(uint32_t tip_ms) {
    tips++;

    if(tip_ms - last_tip_ms >= 60000) {
        rain_duration++;
    }

    last_tip_ms = tip_ms;
}

static void tipping_rainfall_config(void) { return; }
static void tipping_rainfall_error_handler(void) { return; }
