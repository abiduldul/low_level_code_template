#ifndef __UART_PROCESS_H__
#define __UART_PROCESS_H__

#include <stm32l4p5xx.h>

#include "CircularBuffer.h"

#define UART_BUFFER_SIZE    4096

typedef struct {
    USART_TypeDef* usart;
    void (*usart_transmit)(const uint8_t*, uint16_t);
    DMA_Channel_TypeDef* dma_channel;

    CircularBuffer_t circularBuffer;

    uint8_t id;

    uint8_t uart_circularbuffer[UART_BUFFER_SIZE];
    uint8_t uart_rx_buffer[UART_BUFFER_SIZE];

    uint16_t rx_read_pos;
} UartProcess_t;

void UartProcess_Init(UartProcess_t* uartProcess, uint8_t id, USART_TypeDef* usart, 
    DMA_Channel_TypeDef* dma_channel, void (*usart_transmit)(const uint8_t*, uint16_t));
void UartProcess_Callback(UartProcess_t* uartProcess);
void UartProcess_Loop();

#endif