#ifndef __MODBUS_CLIENT_H__
#define __MODBUS_CLIENT_H__

#include <stdint.h>

uint8_t ModbusClient_Init();
uint8_t ModbusClient_ReadRegisters(uint8_t server_address, uint8_t function_code, 
    uint16_t start_address, uint16_t quantity, uint16_t* dst_buffer, uint16_t timeout_ms);
uint8_t ModbusClient_WriteRegisters(uint8_t server_address, uint8_t function_code,
    uint16_t start_address, const uint16_t* src_buffer, uint16_t length, 
    uint16_t timeout_ms);
#endif