#include "main.h"

#include <string.h>
#include <stddef.h>
#include <stdint.h>

void uart_transmit_byte(USART_TypeDef* UART, char b) {
    while(!(UART->ISR & USART_ISR_TXE_TXFNF));

    UART->TDR = b;

    return;
}

void uart_transmit_bytes(USART_TypeDef* UART, const char* bs, size_t length) {
    while(length--) {
        uart_transmit_byte(UART, *(bs++));
    }

    return;
}

void uart_transmit_string(USART_TypeDef* UART, const char* string) {
    uart_transmit_bytes(UART, string, strlen(string));

    return;
}