#ifndef __RTOS_H__
#define __RTOS_H__

#include <stdint.h>
#include "tx_api.h"

uint8_t* RTOS_AllocateMem(ULONG size);
uint16_t RTOS_CreateThread(TX_THREAD* tx, char* name, void (*entry_function)(ULONG id), 
        uint64_t stack_size, uint16_t priority, uint8_t start);
uint16_t RTOS_CreateQueue(TX_QUEUE* queue, char* name, uint32_t queue_size);
#endif