#include "sensor.h"
#include "ModbusClient.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "utils.h"

#define SENSOR_ADDRESS                  ((uint8_t)0x08)
#define SENSOR_REGISTERS                ((uint16_t)0x0000)
#define SENSOR_FUNCTIONCODE_READ        ((uint8_t)0x03)

#define SENSOR_VALUE_TEMPERATURE        0
#define SENSOR_VALUE_HUMIDITY           1
#define SENSOR_VALUE_PRESSURE           2
#define SENSOR_VALUE_WINDSPEED          3
#define SENSOR_VALUE_WINDDIRECTION      4

#define POSITION_VALUE_TEMPERATURE      0
#define POSITION_VALUE_HUMIDITY         1
#define POSITION_VALUE_PRESSURE         6
#define POSITION_VALUE_WINDSPEED        11
#define POSITION_VALUE_WINDDIRECTION    12

static void bgt_w87x_init();
static uint8_t bgt_w87x_read();
static void bgt_w87x_error_handler();
static void bgt_w87x_config();

static float temperature;
static float humidity;
static float pressure;
static float windspeed;
static uint16_t winddirection;

static SensorValue_t _values[] = {
    {
    .data_type = DATATYPE_FLOAT,
    .value = &temperature
    }, {
    .data_type = DATATYPE_FLOAT,
    .value = &humidity
    }, {
    .data_type = DATATYPE_FLOAT,
    .value = &pressure
    }, {
    .data_type = DATATYPE_FLOAT,
    .value = &windspeed
    }, {
    .data_type = DATATYPE_UINT16,
    .value = &winddirection
    }
};
Sensor_t bgt_w87x = {
    .id = 0x1232,
    .init = bgt_w87x_init,
    .read = bgt_w87x_read,
    .error_handler = bgt_w87x_error_handler,
    .config = bgt_w87x_config,
    .nb_values = 5,
    .values = _values
};

static void bgt_w87x_init() {
    return;
}

static void bgt_w87x_config() {
    return;
}

static void bgt_w87x_error_handler() {
    return;
}

static uint8_t bgt_w87x_read() {
    uint16_t buffer[16];

    if(ModbusClient_ReadRegisters(SENSOR_ADDRESS, SENSOR_FUNCTIONCODE_READ, 
        SENSOR_REGISTERS, 0x0D, buffer, 2000) != 1) {
        
        return -1;
    }

    if(buffer[POSITION_VALUE_TEMPERATURE] != 0x7FFF) {
        temperature = (swap16(buffer[POSITION_VALUE_TEMPERATURE]) * 0.1);
    }

    if(buffer[POSITION_VALUE_HUMIDITY] != 0x7FFF) {
        humidity = (swap16(buffer[POSITION_VALUE_HUMIDITY]) * 0.1);
    }
    
    if(buffer[POSITION_VALUE_PRESSURE] != 0x7FFF) {
        pressure = (swap16(buffer[POSITION_VALUE_PRESSURE]) * 0.1);
    }

    if(buffer[POSITION_VALUE_WINDSPEED] != 0x7FFF) {
        windspeed = (swap16(buffer[POSITION_VALUE_WINDSPEED]) * 0.1);
    }

    if(buffer[POSITION_VALUE_WINDDIRECTION] != 0x7FFF) {
        winddirection = (swap16(buffer[POSITION_VALUE_WINDDIRECTION]));
    }

    return 1;
}