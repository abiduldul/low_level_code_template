#include "sensor.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static Sensor_t* sensorList[8];
static uint16_t nbOfSensor;

static char* __strdup(const char* str) {
    uint16_t str_len = strlen(str);
    char* ret = malloc(str_len + 1);

    if(ret == NULL) return NULL;

    memcpy(ret, str, str_len + 1);
    return ret;
}

void Sensor_Init(Sensor_t* sensor, uint8_t* actives, const char** ids) {
    for(uint16_t i = 0; i < sensor->nb_values; i++) {
        if(actives[i] == 1) {
            sensor->values[i].used = 1;
            sensor->values[i].id = __strdup(ids[i]);
        } else {
            sensor->values[i].used = 0;
            sensor->values[i].id = NULL;
        }
    }

    sensorList[nbOfSensor++] = sensor;
    
    sensor->init();

    return;
}

void Sensor_ErrorHandler(Sensor_t* sensor) {
    sensor->error_handler(sensor->error_codes);
}

uint8_t Sensor_Read(Sensor_t* sensor) {
    return sensor->read();
}

uint8_t Sensor_ReadAll() {
    uint8_t status = 1;

    for(uint16_t i = 0; i < nbOfSensor; i++) {
        if(Sensor_Read(sensorList[i]) != 1) {
            status = -1;
        }
    }

    return status;
}

void Sensor_PackedAllJson(char* dst) {
    char* pDst = dst;

    for(uint16_t i = 0; i < nbOfSensor; i++) {
        uint16_t length = Sensor_PackedJson(sensorList[i], pDst);
        pDst += length;
        
        *(pDst - 1) = ',';
    }

    *(pDst - 1) = 0;

    return;
}

uint16_t Sensor_PackedJson(Sensor_t* sensor, char* dst) {
    char* tmp = dst;

    for(uint16_t i = 0; i < sensor->nb_values; i++) {
        if(sensor->values[i].used != 1) {
            continue;
        }

        tmp += sprintf(tmp, "\"%s\":", sensor->values[i].id);

        switch(sensor->values[i].data_type) {
            case DATATYPE_FLOAT:
            tmp += sprintf(tmp, "%.2f", *(float*)sensor->values[i].value);
            break;

            case DATATYPE_UINT16:
            tmp += sprintf(tmp, "%"PRIu16"", *(uint16_t*)sensor->values[i].value);
            break;

            case DATATYPE_INT16:
            tmp += sprintf(tmp, "%"PRId16"", *(int16_t*)sensor->values[i].value);
            break;

            case DATATYPE_INT32:
            tmp += sprintf(tmp, "%"PRId32"", *(int32_t*)sensor->values[i].value);
            break;

            case DATATYPE_UINT32:
            tmp += sprintf(tmp, "%"PRIu32"", *(uint32_t*)sensor->values[i].value);
            break;

            default:
            break;
        }

        *(tmp++) = ',';
    }

    return tmp - dst;
}