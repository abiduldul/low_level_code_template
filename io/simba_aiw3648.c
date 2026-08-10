#include "sensor.h"
#include "ModbusClient.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "utils.h"

#define SENSOR_ADDRESS                  ((uint8_t)0x0B)
#define SENSOR_REGISTERS                ((uint16_t)0x1000)
#define SENSOR_FUNCTIONCODE_READ        ((uint8_t)0x03)

#define SENSOR_VALUE_WATERLEVEL         4
#define SENSOR_VALUE_TEMPERATURE        1

#define POSITION_VALUE_WATERLEVEL       2
#define POSITION_VALUE_TEMPERATURE      12

static float __simba_aiw3648_get_waterlevel(uint8_t* p);
static float __simba_aiw3648_get_temperature(uint8_t* p);

static void simba_aiw3648_init();
static uint8_t simba_aiw3648_read();
static void simba_aiw3648_error_handler();
static void simba_aiw3648_config();

static float waterlevel;
static float temperature;

static SensorValue_t _values[] = {
    {
    .data_type = DATATYPE_FLOAT,
    .value = &waterlevel
    }, {
    .data_type = DATATYPE_FLOAT,
    .value = &temperature
    }
};

Sensor_t simba_aiw3648 = {
    .id = 0x4122,
    .init = simba_aiw3648_init,
    .read = simba_aiw3648_read,
    .error_handler = simba_aiw3648_error_handler,
    .config = simba_aiw3648_config,
    .nb_values = 2,
    .values = _values
};

static void simba_aiw3648_init() {
    return;
}
static uint8_t simba_aiw3648_read() {
    uint16_t buffer[14];

    if(ModbusClient_ReadRegisters(SENSOR_ADDRESS, SENSOR_FUNCTIONCODE_READ,
        SENSOR_REGISTERS, 0x0E, buffer, 1000) != 1) {
            return -1;
    }

    waterlevel = __simba_aiw3648_get_waterlevel((uint8_t*)(buffer + POSITION_VALUE_WATERLEVEL));
    temperature = __simba_aiw3648_get_temperature((uint8_t*)(buffer + POSITION_VALUE_TEMPERATURE));

    return 1;
}
static void simba_aiw3648_error_handler() {
    return;
}
static void simba_aiw3648_config() {
    return;
}

static float __simba_aiw3648_get_waterlevel(uint8_t* p) {
    uint8_t tmp_buf[4] = {*(p + 3), *(p + 2), *(p + 1), *p};
	float ret = *((float*)tmp_buf);

    return ret;
}

static float __simba_aiw3648_get_temperature(uint8_t* p) {
    uint8_t tmp_buf[4] = {*(p + 3), *(p + 2), *(p + 1), *p};
	float ret = *((float*)tmp_buf);

    return ret;
}