/**
 ******************************************************************************
 * rs485.h - USART1 in RS485 mode with circular DMA RX + IDLE framing
 ******************************************************************************
 */

#ifndef RS485_H
#define RS485_H

#include <stdint.h>

#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Low-level setup for USART1 (clocks, GPIO, DMA1_Channel3, NVIC).
 * Call these from the project-wide HAL_UART_MspInit/MspDeInit dispatcher
 * when huart->Instance == USART1. */
void RS485_MspInit(UART_HandleTypeDef *huart);
void RS485_MspDeInit(UART_HandleTypeDef *huart);

/* Call these from the project-wide HAL_UARTEx_RxEventCallback /
 * HAL_UART_ErrorCallback dispatchers when huart->Instance == USART1. */
void RS485_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size);
void RS485_ErrorCallback(UART_HandleTypeDef *huart);

/* Configures USART1, its GPIOs, DMA1 Channel 3 and starts reception. */
void RS485_Init(void);

/* Blocking transmit; DE is driven by hardware. */
void RS485_Send(const uint8_t *buf, uint16_t length);

/* Called from interrupt context once an idle line terminates a frame.
 * 'buffer' is only valid for the duration of the call - copy it if needed. */
void RS485_SetRx_Callback(void (*cb)(const uint8_t *buffer, uint16_t length));

#ifdef __cplusplus
}
#endif

#endif /* RS485_H */