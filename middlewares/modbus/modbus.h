#ifndef __MODBUS_MASTER_H__
#define __MODBUS_MASTER_H__

#include <stdint.h>

typedef struct {
    uint8_t (*callback)(const uint8_t* buffer, uint16_t* length);
    uint8_t (*send)(const uint8_t* buffer, uint16_t length);
    uint8_t mode;
} modbus_master_t;

typedef struct {
    uint8_t (*callback)(const uint8_t* buffer, uint16_t* length);
    uint8_t (*receive)(uint8_t* buffer, uint16_t length);
} modbus_slave_t;

enum {
    MODBUS_MODE_CLIENT = 0,
    MODBUS_MODE_SERVER
};

void modbus_init(modbus_t* modbus, uint8_t (*callback)(const uint8_t*, uint16_t), 
    uint8_t (*send)(const uint8_t*, uint16_t), uint8_t mode);

#endif