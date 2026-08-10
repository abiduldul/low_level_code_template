#include "rtos.h"

#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <rs485.h>

#include "utils.h"

enum {
    REQUEST_TYPE_READ = 0,
    REQUEST_TYPE_WRITE
};

typedef struct {
    uint8_t type;
    uint8_t status;

    uint8_t server_address;
    uint8_t function_code;
    uint16_t start_address;
    uint16_t quantity;
    uint16_t* buffer;
    uint16_t timeout_ms;

    TX_SEMAPHORE sync_sem;
} ModbusClient_Request_t;

static uint16_t modbus_crc(const uint8_t* buffer, uint16_t length);
static void modbus_send(const uint8_t* data, uint16_t length);
static uint8_t modbusclient_requesthandler(ModbusClient_Request_t* request);
static void modbusclient_thread(ULONG input);
uint8_t modbusclient_request(ModbusClient_Request_t* request);
static uint8_t modbus_receive_wait(uint32_t timeout_ms);
static const char* modbus_print_hex(uint8_t* buf, uint16_t length);
static uint8_t modbus_parse_read(const uint8_t* data, uint8_t address, 
    uint16_t length, uint8_t* dst_buffer);

static void modbusclient_thread(ULONG input);
static void rs485_receive_callback(const uint8_t* data, uint16_t length);

static TX_THREAD tx_modbusclient;
static TX_QUEUE modbusclient_queue;
static TX_SEMAPHORE rx_callback_sem;
static uint8_t raw_rx_buffer[256];
static uint16_t raw_rx_length;

uint8_t ModbusClient_Init() {
    RTOS_CreateThread(&tx_modbusclient, "ModbusClient", modbusclient_thread, 4096, 10, 1);

    RTOS_CreateQueue(&modbusclient_queue, "ModbusClientQ", 8);

    RS485_Init();
    RS485_SetRx_Callback(rs485_receive_callback);
    
    tx_semaphore_create(&rx_callback_sem, "ModbusCB", 0);

    return 1;
}

uint8_t ModbusClient_ReadRegisters(uint8_t server_address, uint8_t function_code, 
    uint16_t start_address, uint16_t quantity, uint16_t* dst_buffer, uint16_t timeout_ms) {
    
    ModbusClient_Request_t* packetRead = malloc(sizeof(ModbusClient_Request_t));

    if(packetRead == NULL) return -1;

    packetRead->type           = REQUEST_TYPE_READ;
    packetRead->server_address = server_address;
    packetRead->function_code  = function_code;
    packetRead->start_address  = start_address;
    packetRead->quantity       = quantity;
    packetRead->buffer         = dst_buffer;
    packetRead->timeout_ms     = timeout_ms;

    uint8_t status = modbusclient_request(packetRead);

    free(packetRead);

    return status;
}
    
uint8_t ModbusClient_WriteRegisters(uint8_t server_address, uint8_t function_code,
    uint16_t start_address, const uint16_t* src_buffer, uint16_t length,  
    uint16_t timeout_ms) {
    ModbusClient_Request_t* packetWrite = malloc(sizeof(ModbusClient_Request_t));

    if(packetWrite == NULL) return -1;

    packetWrite->type           = REQUEST_TYPE_WRITE;
    packetWrite->server_address = server_address;
    packetWrite->function_code  = function_code;
    packetWrite->start_address  = start_address;
    packetWrite->quantity       = length;
    packetWrite->buffer         = src_buffer;
    packetWrite->timeout_ms     = timeout_ms;

    uint8_t status = modbusclient_request(packetWrite);

    free(packetWrite);

    return status;
}

uint8_t modbusclient_request(ModbusClient_Request_t* request) {
    tx_semaphore_create(&request->sync_sem, NULL, 0);

    if(tx_queue_send(&modbusclient_queue, &request, TX_WAIT_FOREVER) != TX_SUCCESS) {
        return -1;
    }

    tx_semaphore_get(&request->sync_sem, TX_WAIT_FOREVER);

    uint8_t request_status = request->status;

    tx_semaphore_delete(&request->sync_sem);

    return request_status;
}

static void modbusclient_thread(ULONG input) {
    while(1) {
        ModbusClient_Request_t* request;

        if(tx_queue_receive(&modbusclient_queue, &request, TX_WAIT_FOREVER) == TX_SUCCESS) {
            request->status = modbusclient_requesthandler(request);

            tx_semaphore_put(&request->sync_sem);
        }
    }

    return;
}

static uint8_t modbusclient_requesthandler(ModbusClient_Request_t* request) {
    uint16_t buffer_length = request->type == REQUEST_TYPE_READ ? 8 : 
        (4 + (request->quantity * 2) + 2);
    
    uint8_t buffer[buffer_length];

    buffer[0] = request->server_address;
    buffer[1] = request->function_code;
    buffer[2] = (uint8_t)(request->start_address >> 8);
    buffer[3] = (uint8_t)request->start_address;

    if(request->type == REQUEST_TYPE_READ) {
        buffer[4] = (uint8_t)(request->quantity >> 8);
        buffer[5] = (uint8_t)request->quantity;
    } else if(request->type == REQUEST_TYPE_WRITE) {
        for(uint16_t i = 0; i < request->quantity; i++) {
            *((uint16_t*)(buffer + 4) + i) = swap16(*(request->buffer + i));
        }
    }

    uint16_t crc = modbus_crc(buffer, buffer_length - 2);

    buffer[buffer_length - 1] = (uint8_t)(crc >> 8);
    buffer[buffer_length - 2] = (uint8_t)crc;

    LOG_INFO("Send\t:%s", modbus_print_hex(buffer, buffer_length));

    while(tx_semaphore_get(&rx_callback_sem, TX_NO_WAIT) == TX_SUCCESS);

    uint16_t expected_length = 3 + (request->quantity * 2) + 2; 
    uint8_t received_buffer[expected_length];

    modbus_send(buffer, buffer_length);

    if(request->type == REQUEST_TYPE_WRITE) return 1;

    do {
        if(modbus_receive_wait(request->timeout_ms) != 1) raw_rx_length = 0;
        else {
            LOG_INFO("Arrived: %s", modbus_print_hex(raw_rx_buffer, raw_rx_length));
        }
    } while((request->status = modbus_parse_read(raw_rx_buffer, 
        request->server_address, raw_rx_length, received_buffer)) == 0);

    switch(request->status) {
        case 1:
            memcpy(request->buffer, received_buffer + 3, received_buffer[2]);
            return 1;
        break;

        case 255:
            LOG_ERROR("Receive timeout");
            return -1;
        break;

        case 254:
            LOG_ERROR("Receive wrong CRC");
            return -2;
        break;

        default:
            LOG_ERROR("Receive unknown");
    }

    return -3;
}

static void modbus_send(const uint8_t* data, uint16_t length) {
    RS485_Send(data, length);

    return;
}

enum {
    MODBUS_STATE_PARSE_ADDRESS = 0,
    MODBUS_STATE_PARSE_COMMAND,
    MODBUS_STATE_PARSE_LENGTH,
    MODBUS_STATE_PARSE_COPY_REGISTERS,
    MODBUS_STATE_PARSE_CRC
};

static uint8_t modbus_parse_read(const uint8_t* data, uint8_t address, 
    uint16_t length, uint8_t* dst_buffer) {
    static uint8_t state;
    static uint16_t count;
    static uint8_t copy_registers_count;

    if(length == 0) {
        state = MODBUS_STATE_PARSE_ADDRESS;
        count = 0;
        copy_registers_count = 0;

        return -1;
    }

    while(length--) {
        switch(state) {
            case MODBUS_STATE_PARSE_ADDRESS:
            if(address == *(data++)) {
                dst_buffer[count++] = *(data - 1);

                state = MODBUS_STATE_PARSE_COMMAND; 
            }

            break;
            case MODBUS_STATE_PARSE_COMMAND:
            dst_buffer[count++] = *(data++);

            state = MODBUS_STATE_PARSE_LENGTH;

            break;
            case MODBUS_STATE_PARSE_LENGTH:
            dst_buffer[count++] = *(data++);
            copy_registers_count = 0;

            state = MODBUS_STATE_PARSE_COPY_REGISTERS;
            break;
        
            case MODBUS_STATE_PARSE_COPY_REGISTERS:
            dst_buffer[count++] = *(data++);

            if(copy_registers_count++ == dst_buffer[2] - 1) {
                state = MODBUS_STATE_PARSE_CRC;
            }
            break;

            case MODBUS_STATE_PARSE_CRC:
            dst_buffer[count] = data[0];
            dst_buffer[count + 1] = data[1];

            uint16_t crc_swapped = modbus_crc(dst_buffer, count);
            uint8_t status = *((uint16_t*)(data)) == crc_swapped ? 1 : -2;

            //printf("crc data: %04X, crc calculation: %04X\r\n", *(uint16_t*)(data), crc_swapped);

            count = 0;
            copy_registers_count = 0;

            state = MODBUS_STATE_PARSE_ADDRESS;            
            return status;

            break;
        }
    }

    return 0;
}

static uint8_t modbus_receive_wait(uint32_t timeout_ms) {
    //tx_semaphore_put(&rx_callback_sem);

/*    uint8_t test_buffer[] = {0x08, 0x03, 0x1A, 0x01, 0x18, 
        0x02, 0xBF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 
        0xFF, 0x26, 0xB8, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF,
        0x7F, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x3D, 0x02};

    memcpy(raw_rx_buffer, test_buffer, sizeof(test_buffer));

    raw_rx_length = sizeof(test_buffer);*/

    //LOG_INFO("Waiting");
    if(tx_semaphore_get(&rx_callback_sem, timeout_ms) != TX_SUCCESS) {
        LOG_ERROR("Timeout!");
        
        return -1;
    }

    return 1;
}

static const char* modbus_print_hex(uint8_t* buf, uint16_t length) {
    static char modbus_debug[256];

    uint16_t length_debug = 0;

    for(uint16_t i = 0; i < length; i++) {
        length_debug += snprintf(modbus_debug + length_debug, 
            sizeof(modbus_debug) - length_debug, "%02X ", buf[i]);
    }

    return modbus_debug;
}

static void rs485_receive_callback(const uint8_t* data, uint16_t length) {
    memcpy(raw_rx_buffer, data, length);
    raw_rx_length = length;
    //LOG_INFO("Arrived: %s, length: %u", 
    //    modbus_print_hex(raw_rx_buffer, raw_rx_length), raw_rx_length);

    tx_semaphore_put(&rx_callback_sem);
}

static uint16_t modbus_crc(const uint8_t* buf, uint16_t len) {
	static const uint16_t table[256] = {
	0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
	0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
	0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
	0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
	0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
	0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
	0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
	0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
	0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
	0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
	0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
	0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
	0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
	0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
	0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
	0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
	0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
	0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
	0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
	0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
	0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
	0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
	0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
	0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
	0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
	0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
	0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
	0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
	0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
	0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
	0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
	0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040 };

	uint8_t xor = 0;
	uint16_t crc = 0xFFFF;

	while( len-- )
	{
		xor = (*buf++) ^ crc;
		crc >>= 8;
		crc ^= table[xor];
	}

	return crc;
}