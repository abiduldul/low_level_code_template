#ifndef __MAIN_H__
#define __MAIN_H__

#include "stm32l4p5xx.h"

#include <stddef.h>
#include <stdint.h>

#define PCLK    80000000

void Init(void);

void Clock_Config(void);
void OutputPin_Config();
void LPUART1_Config(void);
void USART1_Config(void);
void USART2_Config(void);
void USART3_Config(void);
void UART4_Config(void);

void delay(uint32_t ms);
uint32_t tick();

void uart_transmit_byte(USART_TypeDef* UART, char b);
void uart_transmit_bytes(USART_TypeDef* UART, const char* bs, size_t length);
void uart_transmit_string(USART_TypeDef* UART, const char* string);

void lpuart1_transmit(const uint8_t *data, uint16_t length);
void lpuart1_set_cb(void (*lpuart1_cb)(const uint8_t*, uint16_t));
void lpuart1_rx_loop();

void usart3_transmit(const uint8_t *data, uint16_t length);

#endif