#include <stdint.h>
#include "tx_api.h"
#include "rtos.h"
#include "App.h"

#define THREAD_MEM_POOL_SIZE    (64 * 1024)
#define THREAD_TX_THREAD_BLOCK_POOL 16

static TX_BYTE_POOL tx_byte_pool;
static uint8_t tx_byte_pool_buffer[THREAD_MEM_POOL_SIZE];

uint8_t* RTOS_AllocateMem(ULONG size) {
	uint8_t* ptr;

	if(tx_byte_allocate(&tx_byte_pool, (VOID**)&ptr,
				size, TX_NO_WAIT) != TX_SUCCESS) {
		return TX_NULL;
	}

	return ptr;
}

uint16_t RTOS_CreateThread(TX_THREAD* tx, char* name, void (*entry_function)(ULONG id), 
        uint64_t stack_size, uint16_t priority, uint8_t start) {
    uint8_t* ptr = TX_NULL;
    uint16_t ret;

    if((ptr = RTOS_AllocateMem(stack_size)) == TX_NULL) {
        return TX_THREAD_ERROR;
    }

    if((ret = tx_thread_create(tx, name, entry_function, 0, ptr, stack_size, priority, 
        priority, TX_NO_TIME_SLICE, start)) != TX_SUCCESS) {
        return ret;
    }

    return TX_SUCCESS;
}

uint16_t RTOS_CreateQueue(TX_QUEUE* queue, char* name, uint32_t queue_size) {
	uint64_t allocate_size = queue_size * sizeof(uint64_t);
	uint16_t ret;
	uint8_t* ptr;

	if((ptr = RTOS_AllocateMem(allocate_size)) == TX_NULL) return TX_QUEUE_ERROR;

	if((ret = tx_queue_create(queue, name, 1, ptr, 
        queue_size * sizeof(ULONG))) != TX_SUCCESS) {
		return ret;
	}

	return TX_SUCCESS;
}

void tx_application_define(void* first_unused_memory) {
    if(tx_byte_pool_create(&tx_byte_pool, "Thread Memory Pool", tx_byte_pool_buffer, 
        THREAD_MEM_POOL_SIZE) != TX_SUCCESS) {
        while(1);
    }

    App_Init(app_active);

    return;
}