/**
 ******************************************************************************
 * rs485.c - USART1 in RS485 mode with circular DMA RX + IDLE framing
 *
 * HAL version (converted from the register-level implementation).
 *
 *   PA9  = USART1_TX  (AF7)
 *   PA10 = USART1_RX  (AF7)
 *   PB3  = USART1_DE  (AF7, RS485 driver enable)
 *   DMA1 Channel 3, DMAMUX request USART1_RX
 *
 * Requirements:
 *   - HAL_UART_MODULE_ENABLED and HAL_DMA_MODULE_ENABLED
 *   - stm32l4xx_hal_uart.c, _uart_ex.c, _dma.c, _dma_ex.c in the build
 *   - A HAL version with HAL_UARTEx_ReceiveToIdle_DMA() and RxEventType
 *     (STM32L4 HAL >= 1.14). See the fallback note near the callback.
 ******************************************************************************
 */

#include "rs485.h"

// #include "stm32l4xx_hal.h"
    
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */

#define USART1_RX_BUFFER_SIZE   64U     /* circular DMA landing buffer   */
#define RS485_RX_BUFFER_SIZE    256U    /* assembled frame buffer        */

#define RS485_BAUDRATE          9600U
#define RS485_TX_TIMEOUT_MS     1000U

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

static UART_HandleTypeDef huart1;
static DMA_HandleTypeDef  hdma_usart1_rx;

static uint8_t  usart1_rx_buffer[USART1_RX_BUFFER_SIZE];

static uint8_t  rs485_rx_buffer[RS485_RX_BUFFER_SIZE];
static uint16_t rs485_rx_length;
static uint16_t rs485_rx_old_pos;
static uint8_t  rs485_rx_overflow;

static void (*p_rx_cb)(const uint8_t *buffer, uint16_t length);

static void rs485_collect(uint16_t curr_pos);
static void rs485_restart_rx(void);

/* ------------------------------------------------------------------------- */
/* MSP - clocks, GPIO, DMA, NVIC                                             */
/*                                                                           */
/* HAL_UART_MspInit() is a single __weak hook shared by EVERY UART in the    */
/* project, so it can only be defined once. This module therefore exports    */
/* RS485_MspInit() / RS485_MspDeInit() and expects the project-wide          */
/* HAL_UART_MspInit() (in system.c) to dispatch on huart->Instance:          */
/*                                                                           */
/*   void HAL_UART_MspInit(UART_HandleTypeDef *huart) {                      */
/*       if (huart->Instance == USART3) { ... existing USART3 setup ... }    */
/*       else if (huart->Instance == USART1) { RS485_MspInit(huart); }       */
/*   }                                                                       */
/* ------------------------------------------------------------------------- */

void RS485_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) {
        return;
    }

    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMAMUX1_CLK_ENABLE();

    /* PA9 = TX, PA10 = RX */
    gpio.Pin       = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;           /* idle-high line */
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PB3 = DE */
    gpio.Pin       = GPIO_PIN_3;
    gpio.Pull      = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* DMA1 Channel 3 <- USART1_RX, circular */
    hdma_usart1_rx.Instance                 = DMA1_Channel3;
    hdma_usart1_rx.Init.Request             = DMA_REQUEST_USART1_RX;
    hdma_usart1_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_usart1_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK) {
        return;
    }

    __HAL_LINKDMA(huart, hdmarx, hdma_usart1_rx);

    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

void RS485_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) {
        return;
    }

    __HAL_RCC_USART1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_3);
    HAL_DMA_DeInit(&hdma_usart1_rx);
    HAL_NVIC_DisableIRQ(DMA1_Channel3_IRQn);
    HAL_NVIC_DisableIRQ(USART1_IRQn);
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void RS485_Init(void)
{
    huart1.Instance                    = USART1;
    huart1.Init.BaudRate               = RS485_BAUDRATE;
    huart1.Init.WordLength             = UART_WORDLENGTH_8B;
    huart1.Init.StopBits               = UART_STOPBITS_1;
    huart1.Init.Parity                 = UART_PARITY_NONE;
    huart1.Init.Mode                   = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.Init.ClockPrescaler         = UART_PRESCALER_DIV1;   /* PRESC = 0 */
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    /* Sets CR3.DEM and the DE assertion/deassertion times. Use
     * UART_DE_POLARITY_LOW here if your transceiver needs an active-low DE
     * (that is the CR3.DEP bit the old comment mentioned). */
    if (HAL_RS485Ex_Init(&huart1, UART_DE_POLARITY_HIGH, 0U, 0U) != HAL_OK) {
        return;
    }

    /* CR1.FIFOEN plus thresholds */
    (void)HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8);
    (void)HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8);
    (void)HAL_UARTEx_EnableFifoMode(&huart1);

    rs485_restart_rx();
}

void RS485_Send(const uint8_t *buf, uint16_t length)
{
    /* DE is asserted and released by hardware; blocking transmit matches the
     * behaviour of the old polling loop on TXE_TXFNF. */
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, length, RS485_TX_TIMEOUT_MS);
}

void RS485_SetRx_Callback(void (*cb)(const uint8_t *buffer, uint16_t length))
{
    p_rx_cb = cb;
}

/* ------------------------------------------------------------------------- */
/* IRQ handlers                                                              */
/*                                                                           */
/* NOTE: delete these if stm32l4xx_it.c already defines them - put the two   */
/* HAL_*_IRQHandler() calls in the CubeMX versions instead.                  */
/* ------------------------------------------------------------------------- */

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void DMA1_Channel3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart1_rx);
}

/* ------------------------------------------------------------------------- */
/* HAL callbacks                                                             */
/* ------------------------------------------------------------------------- */

/* HAL_UARTEx_RxEventCallback and HAL_UART_ErrorCallback are single __weak
 * hooks shared by every UART, so this module exports its own and expects the
 * project-wide dispatcher (system.c) to route by huart->Instance.
 *
 * Called for half-transfer, transfer-complete AND idle-line events, which is
 * exactly the set the old code handled in two separate ISRs. 'Size' is the
 * DMA write position measured from the start of the buffer. */
void RS485_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance != USART1) {
        return;
    }

    rs485_collect(Size);

    /* Only an idle line ends a frame. If your HAL predates RxEventType,
     * replace this condition with a check that Size is neither
     * USART1_RX_BUFFER_SIZE nor USART1_RX_BUFFER_SIZE/2 - less exact, since
     * a frame can legitimately end on those boundaries. */
    if (HAL_UARTEx_GetRxEventType(huart) != HAL_UART_RXEVENT_IDLE) {
        return;
    }

    if (rs485_rx_length > 0U) {
        if (!rs485_rx_overflow && (p_rx_cb != NULL)) {
            p_rx_cb(rs485_rx_buffer, rs485_rx_length);
        }
        rs485_rx_length   = 0U;
        rs485_rx_overflow = 0U;
    }
}

void RS485_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) {
        return;
    }

    /* HAL has already cleared ORE/NE/FE/PE. In circular mode reception
     * usually survives; restart only if the DMA actually stopped. */
    if (HAL_UART_GetState(huart) != HAL_UART_STATE_BUSY_RX) {
        rs485_restart_rx();
    }
}

/* ------------------------------------------------------------------------- */
/* Internals                                                                 */
/* ------------------------------------------------------------------------- */

static void rs485_restart_rx(void)
{
    rs485_rx_old_pos  = 0U;
    rs485_rx_length   = 0U;
    rs485_rx_overflow = 0U;

    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart1, usart1_rx_buffer,
                                       USART1_RX_BUFFER_SIZE);
}

/* Copy everything the DMA wrote since the previous event into the frame
 * buffer, handling the wrap. Same algorithm as RS485_UART_DMA_Rx(), but the
 * position comes from HAL instead of being read out of CNDTR. */
static void rs485_collect(uint16_t curr_pos)
{
    if (curr_pos == rs485_rx_old_pos) {
        return;
    }

    if (curr_pos > rs485_rx_old_pos) {
        /* Linear: [ --- R DDDD W --- ] */
        uint16_t length = curr_pos - rs485_rx_old_pos;

        if ((rs485_rx_length + length) <= RS485_RX_BUFFER_SIZE) {
            memcpy(&rs485_rx_buffer[rs485_rx_length],
                   &usart1_rx_buffer[rs485_rx_old_pos], length);
            rs485_rx_length += length;
        } else {
            rs485_rx_overflow = 1U;
        }
    } else {
        /* Wrapped: [ DDD W ----- R DDD ] */
        uint16_t length_to_end = USART1_RX_BUFFER_SIZE - rs485_rx_old_pos;

        if ((rs485_rx_length + length_to_end + curr_pos) <= RS485_RX_BUFFER_SIZE) {
            if (length_to_end > 0U) {
                memcpy(&rs485_rx_buffer[rs485_rx_length],
                       &usart1_rx_buffer[rs485_rx_old_pos], length_to_end);
                rs485_rx_length += length_to_end;
            }
            if (curr_pos > 0U) {
                memcpy(&rs485_rx_buffer[rs485_rx_length],
                       &usart1_rx_buffer[0], curr_pos);
                rs485_rx_length += curr_pos;
            }
        } else {
            rs485_rx_overflow = 1U;
        }
    }

    rs485_rx_old_pos = curr_pos;
}