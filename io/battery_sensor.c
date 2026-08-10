#include "sensor.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "utils.h"

#include "battery.h"

static uint16_t batt_percentage_value;
static float cpu_temperature_value;

static SensorValue_t _values[] = {
    {
    .data_type = DATATYPE_UINT16,
    .value = &batt_percentage_value
    },
    {
    .data_type = DATATYPE_FLOAT,
    .value = &cpu_temperature_value
    }
};

static void batt_sensor_init();
static uint8_t batt_sensor_read();
static void batt_sensor_error_handler();
static void batt_sensor_config();

Sensor_t batt_sensor = {
    .id = 0x1021,
    .init = batt_sensor_init,
    .read = batt_sensor_read,
    .error_handler = batt_sensor_error_handler,
    .config = batt_sensor_config,
    .nb_values = 2,
    .values = _values
};

static void batt_sensor_init() {
    return;
}

static uint8_t batt_sensor_read() {
    Battery_Sampling();
    batt_percentage_value = Battery_GetPercentage();
    cpu_temperature_value = Battery_GetTemperature();

    return 1;
}

static void batt_sensor_error_handler() {
    return;
}

static void batt_sensor_config() {
    return;
}