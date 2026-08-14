#ifndef __SENSOR_H__
#define __SENSOR_H__

#include <stdint.h>

typedef enum {
    DATATYPE_FLOAT = 0,
    DATATYPE_INT16,
    DATATYPE_INT32,
    DATATYPE_UINT16,
    DATATYPE_UINT32
} SensorValue_DataType_t;

typedef struct {
    char* id;
    void* value;
    uint8_t used;
    SensorValue_DataType_t data_type;
} SensorValue_t;

typedef struct {
    uint16_t id;
    SensorValue_t* values;
    uint16_t nb_values;
    uint8_t error_codes;

    void (*init)();
    uint8_t (*read)();
    void (*config)();
    void (*error_handler)();
} Sensor_t;

void Sensor_Init(Sensor_t* sensor, uint8_t* actives, const char** ids);
uint8_t Sensor_Read(Sensor_t* sensor);
uint16_t Sensor_PackedJson(Sensor_t* sensor, char* dst);
void Sensor_PackedAllJson(char* dst);
uint8_t Sensor_ReadAll();

extern Sensor_t bgt_w87x;
extern Sensor_t simba_aiw3648;
extern Sensor_t batt_sensor;
extern Sensor_t gps_sensor;
extern Sensor_t tipping_rainfall;

#endif