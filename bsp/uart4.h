/**
 ******************************************************************************
 * uart4.h - GPS link on UART4, circular DMA RX + IDLE framing
 *
 * Path: bsp/uart4.h
 *
 *   PA0 = UART4_TX (AF8)
 *   PA1 = UART4_RX (AF8)
 *   DMA1 Channel 4, DMAMUX request UART4_RX, 9600 baud (default L76-LB)
 ******************************************************************************
 */

#ifndef UART4_H
#define UART4_H

#include <stdint.h>

#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configures UART4, its GPIOs, DMA1 Channel 4 and starts reception. */
void UART4_Config(void);

/* Blocking transmit. Only needed for PMTK configuration sentences. */
void UART4_Transmit(const uint8_t *data, uint16_t length);

void UART4_SetRx_Callback(void (*rx_cb)(const uint8_t *buf, uint16_t length));

/* Hooks for the project-wide HAL dispatchers in system.c. Call each when
 * huart->Instance == UART4. */
void UART4_MspInit(UART_HandleTypeDef *huart);
void UART4_MspDeInit(UART_HandleTypeDef *huart);
void UART4_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size);
void UART4_ErrorCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* UART4_H */