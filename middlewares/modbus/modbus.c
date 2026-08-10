#include "modbus_master.h"

void modbus_init(modbus_t* modbus, uint8_t (*callback)(const uint8_t*, uint16_t), 
    uint8_t (*send)(const uint8_t*, uint16_t), uint8_t mode) {
    modbus->mode = mode;

    modbus->callback = callback;
    modbus->send = send;

    return;
}

void modbus_send_sync(modbus_t* modbus, uint8_t address, uint8_t fn_code, 
    uint16_t* length, uint8_t send, uint16_t timeout) {
    
}

void modbus_parse(modbus_t* modbus) {
    if(modbus->mode == MODBUS_MODE_CLIENT) {
        modbus_parse_client(&modbus);
    } else {
        modbus_parse_master(&modbus);
    }
}