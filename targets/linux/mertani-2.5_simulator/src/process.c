#include "main.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "UartProcess.h"

typedef struct {
    uint8_t id;
    uint16_t length;
    uint8_t* data;
} lpuart1_rx_data_t;

enum {
    STATE_LPUART1_IDLE = 0,
    STATE_LPUART1_LENGTH_LOW,
    STATE_LPUART1_LENGTH_HIGH,
    STATE_LPUART1_DATA
};

static void lpuart1_receive_process(const uint8_t* buffer, uint16_t length);
static void lpuart1_process_data(lpuart1_rx_data_t* rx_data);
static uint8_t check_id_list(uint8_t id);
static void lpuart1_parse(uint8_t c);

static lpuart1_rx_data_t lpuart1_rx_data;

void process_init() {
    lpuart1_set_cb(&lpuart1_receive_process);
    //usart3_set_cb(&usart3_receive_process);

    return;
}

static uint8_t check_id_list(uint8_t id) {
    const uint8_t id_list[] = {'1', '2', '3', '4'};

    for(uint8_t i = 0; i < sizeof(id_list); i++) {
        if(id_list[i] == id) return 1;
    }

    return 0;
}

static void lpuart1_receive_process(const uint8_t* buffer, uint16_t length) {
    while(length--) {
        lpuart1_parse(*(buffer++));
    }

    return;
}

static void lpuart1_process_data(lpuart1_rx_data_t* rx_data) {
    Uart_Process_Transmit(rx_data->id, rx_data->data, rx_data->length);

    return;
}

static void lpuart1_parse(uint8_t c) {
    static uint8_t state;
    static uint32_t start_ms_timeout;
    static uint16_t count;

    if(start_ms_timeout != 0) {
        if(tick() - start_ms_timeout > 2500)  {
            start_ms_timeout = 0;
            count = 0;

            state = STATE_LPUART1_IDLE;
        }
    }

    switch(state) {
        case STATE_LPUART1_IDLE:
            if(check_id_list(c) == 1) {
                lpuart1_rx_data.id = c;

                start_ms_timeout = tick();

                state = STATE_LPUART1_LENGTH_LOW;
            }

        break;
        case STATE_LPUART1_LENGTH_LOW:
            lpuart1_rx_data.length = c;

            state = STATE_LPUART1_LENGTH_HIGH;
        break;
        case STATE_LPUART1_LENGTH_HIGH:
            lpuart1_rx_data.length |= (uint16_t)c << 8;

            lpuart1_rx_data.data = malloc(lpuart1_rx_data.length);

            state = STATE_LPUART1_DATA;
        break;
        case STATE_LPUART1_DATA:
            lpuart1_rx_data.data[count++] = c;

            if(count == lpuart1_rx_data.length) {
                count = 0;

                //lpuart1_rx_data.data[lpuart1_rx_data.length] = '\r';
                //lpuart1_rx_data.data[lpuart1_rx_data.length + 1] = '\n';

                lpuart1_process_data(&lpuart1_rx_data);

                free(lpuart1_rx_data.data);

                state = STATE_LPUART1_IDLE;
            }
        break;
    }

    return;
}

/*static uint8_t usart3_transmit_buffer[16384];

void usart3_receive_process(const uint8_t* buffer, uint16_t length) {
    usart3_transmit_buffer[0] = '1';
    usart3_transmit_buffer[1] = (uint8_t)length;
    usart3_transmit_buffer[2] = (uint8_t)(length << 8);
    memcpy(usart3_transmit_buffer + 3, buffer, length);

    lpuart1_transmit(usart3_transmit_buffer, length + 3);

    return;
}*/