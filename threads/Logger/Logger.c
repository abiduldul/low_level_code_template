#include "Logger.h"
#include "rtos.h"
#include "log.h"
#include "debugger.h"
#include "rtc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


typedef struct {
    char* buffer;
    uint16_t length;
} LogRequest_t;

static TX_THREAD logger;
static TX_QUEUE log_queue;
static char buffer_send[2048];
static char buffer_transmit[2048];
static char* p_buffer_send = buffer_send;

static void Logger_Thread(ULONG input);
static void logrequest_handler(LogRequest_t* request);

static void log_process(const char* buffer, uint16_t length) {
    char* log_buffer = malloc(length);

    LogRequest_t* request = malloc(sizeof(LogRequest_t));

    memcpy(log_buffer, buffer, length);

    request->buffer = log_buffer;
    request->length = length;

    tx_queue_send(&log_queue, &log_buffer, TX_WAIT_FOREVER);
    
/*    if(p_buffer_send + length > buffer_send + sizeof(buffer_send)) {
        char* p_tmp = malloc(length);
        memcpy(p_tmp, buffer, length);
        p_tmp[length] = 0;

        tx_queue_send(&log_queue, &p_tmp, TX_WAIT_FOREVER);

        return;
    }

    memcpy(p_buffer_send, buffer, length);

    p_buffer_send += length;
    *p_buffer_send = 0;

    return;
    */
}

uint8_t Logger_Init() {
    RTOS_CreateThread(&logger, "Logger", Logger_Thread, 1024, 10, 1);

    RTOS_CreateQueue(&log_queue, "LogQueue", 16);

    Log_Init(log_process, RTC_GetTimestamp);

    return 1;
}


static void Logger_Thread(ULONG input) {
    while (1) {
        LogRequest_t* request;

/*        if(tx_queue_receive(&log_queue, &dst_ptr, 100) == TX_SUCCESS) {
            memcpy(buffer_transmit, buffer_send, p_buffer_send - buffer_send + 1);
            printf("%s", buffer_transmit);
            printf("%s", dst_ptr);

            free(dst_ptr);

            p_buffer_send = buffer_send;
        }

        if(p_buffer_send > buffer_send) {
            memcpy(buffer_transmit, buffer_send, p_buffer_send - buffer_send + 1);
            printf("%s", buffer_transmit);

            p_buffer_send = buffer_send;
        }
    }*/

        if(tx_queue_receive(&log_queue, &request, 100) == TX_SUCCESS) {
            logrequest_handler(request);

            free(request->buffer);
            free(request);
        }
    }

    return;
}

static void logrequest_handler(LogRequest_t* request) {
    Debugger_Print(request->buffer, request->length);

    return;
}