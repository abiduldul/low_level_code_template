#include "ModbusClient.h"
#include "Logger.h"
#include "log.h"
#include "App.h"

#include <stdio.h>

static void app_testing_init();
static void app_testing_function();

static App_t app_testing_modbusclient = {.app_init = app_testing_init, 
    .app_function = app_testing_function, .app_name = "App Testing", 
    .app_version = "1.0.0"};

REGISTER_APP(app_testing_modbusclient);

uint16_t format_print_hex(char* dst, const uint8_t* src, uint16_t length) {
    uint16_t length_dst = 0;

    for(uint16_t i = 0; i < length; i++) {
        length_dst += sprintf(dst + length_dst, "%02X ", src[i]);
    }

    length_dst -= 1;
    dst[length_dst] = 0;

    return length_dst;
}

static void app_testing_init(void) {
    Logger_Init();
    ModbusClient_Init();

    return;
}

static void app_testing_function() {
    uint32_t counter = 0;
    static uint8_t dst_modbus[128];
    uint16_t src_modbus[8] = {0x0038, 0x2843, 0x8843, 0x5421};
    static char print_hex_buffer[256];

    LOG_INFO("Test dulu!");
    App_Sleep(3000);

    //ModbusClient_ReadRegisters(0x01, 0x03, 0x4C20, 1, dst_modbus, 1000);

    //ModbusClient_WriteRegisters(0x01, 0x03, 0x4C20, src_modbus, 4, 1000);

    while(1) {
        LOG_INFO("Counter: %lu", counter++);

        if(ModbusClient_ReadRegisters(0x08, 0x03, 0x0000, 0xD, 
            (uint16_t*)dst_modbus, 2000) == 1) {
        
            format_print_hex(print_hex_buffer, dst_modbus, 0x0D * 2);
            LOG_INFO("Received: %s", print_hex_buffer);
        }

        App_Sleep(2000);
    }

    return;
}