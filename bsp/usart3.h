/**
 ******************************************************************************
 * usart3.h - ESP32 link on USART3 with circular DMA RX + IDLE framing
 ******************************************************************************
 */

#ifndef USART3_H
#define USART3_H

#include <stdint.h>

#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configures USART3, its GPIOs, DMA1 Channel 2 and starts reception. */
void USART3_Config(void);

/* Blocking transmit. */
void USART3_Transmit(const uint8_t *data, uint16_t length);

/* Called from interrupt context once an idle line terminates a frame.
 * 'buf' is only valid for the duration of the call - copy it if needed. */
void ESP32_SetRx_Callback(void (*rx_cb)(const uint8_t *buf, uint16_t length));

/* Hooks for the project-wide HAL dispatchers in system.c. Call each when
 * huart->Instance == USART3. */
void USART3_MspInit(UART_HandleTypeDef *huart);
void USART3_MspDeInit(UART_HandleTypeDef *huart);
void USART3_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size);
void USART3_ErrorCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* USART3_H */