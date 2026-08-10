#include "rtos.h"

#include "log.h"

#include "sensor.h"

typedef struct {
    void *values;
} StatisticalMethod_t;

typedef struct {
    void *values;
} MovingAverage_t;

typedef struct {
    Sensor_t* sensor;
    void* values;
    uint32_t count;
} SensorDriver_t;

void SensorManager_Init() {
/*    SensorDriver_t myDriver;

    for(uint16_t i = 0; i < myDriver.sensor->nb_values; i++) {
        switch(myDriver.sensor->values->data_type) {
            case DATATYPE_INT8:
            case DATATYPE_UINT8:
            myDriver.values = malloc(myDriver.sensor->nb_values * 1);
            break;
            case DATATYPE_INT16:
            case DATATYPE_UINT16:
            myDriver.values = malloc(myDriver.sensor->nb_values * 2);
            break;
            case DATATYPE_FLOAT:
            case DATATYPE_UINT32:
            case DATATYPE_INT32:
            myDriver.values = malloc(myDriver.sensor->nb_values * 4);
            break;
            default:
            myDriver.values = NULL;
            break;
        }
    }
*/
    RTOS_CreateThread(&tx_sensormanager, "SensorManager Thread", sensormanager_thread, 4096, 10, 1);
    return;
}

static void sensormanager_thread(ULONG input) {
    while(1) {
        
    }
}

void __method(SensorDriver_t* sensorDriver) {
    Sensor_t* pSensor = sensorDriver->sensor;

    for(uint16_t i = 0; i < pSensor->nb_values; i++) {
        switch(pSensor->values[i].data_type) {
            case DATATYPE_INT8:
            case DATATYPE_UINT8:
            *((uint8_t*)sensorDriver->values + i) += 
            *(uint8_t*)pSensor->values[i].value;
            *((uint8_t*)sensorDriver->values + i) /= 2;
            break;
            case DATATYPE_INT16:
            case DATATYPE_UINT16:
            myDriver.values = malloc(myDriver.sensor->nb_values * 2);
            break;
            case DATATYPE_FLOAT:
            case DATATYPE_UINT32:
            case DATATYPE_INT32:
            myDriver.values = malloc(myDriver.sensor->nb_values * 4);
            break;
            default:
            myDriver.values = NULL;
            break;
        }
    }
}