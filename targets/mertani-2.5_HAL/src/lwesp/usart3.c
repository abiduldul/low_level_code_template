/**
 ******************************************************************************
 * usart3.c - ESP32 link on USART3 with circular DMA RX + IDLE framing
 *
 * HAL version (converted from the register-level implementation).
 *
 *   PC4 = USART3_TX (AF7)
 *   PC5 = USART3_RX (AF7)
 *   DMA1 Channel 2, DMAMUX request USART3_RX, 115200 baud
 *
 * All the shared __weak HAL hooks (MspInit, RxEventCallback, ErrorCallback)
 * are exported under module-specific names, because USART1 (rs485.c) needs
 * them too and each hook may only be defined once project-wide. See the
 * dispatcher snippet at the bottom of this file.
 ******************************************************************************
 */

#include "usart3.h"

#include "stm32l4xx_hal.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */

#define USART3_RX_BUFFER_SIZE   512U    /* circular DMA landing buffer */
#define ESP32_RX_BUFFER_SIZE    2048U   /* assembled frame buffer      */

#define USART3_BAUDRATE         115200U
#define USART3_TX_TIMEOUT_MS    1000U

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

static UART_HandleTypeDef huart3;
static DMA_HandleTypeDef  hdma_usart3_rx;

static uint8_t  usart3_rx_buffer[USART3_RX_BUFFER_SIZE];

static uint8_t  esp32_rx_buffer[ESP32_RX_BUFFER_SIZE];
static uint16_t esp32_rx_length;
static uint16_t esp32_rx_old_pos;
static uint8_t  esp32_rx_overflow;

static void (*p_rx_cb)(const uint8_t *buf, uint16_t length);

static void esp32_collect(uint16_t curr_pos);
static void usart3_restart_rx(void);

/* ------------------------------------------------------------------------- */
/* MSP - clocks, GPIO, DMA, NVIC                                             */
/* ------------------------------------------------------------------------- */

void USART3_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3) {
        return;
    }

    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMAMUX1_CLK_ENABLE();

    /* PC4 = TX, PC5 = RX */
    gpio.Pin       = GPIO_PIN_4 | GPIO_PIN_5;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;               /* idle-high line */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH; /* matches OSPEEDR = 11 */
    gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* DMA1 Channel 2 <- USART3_RX, circular */
    hdma_usart3_rx.Instance                 = DMA1_Channel2;
    hdma_usart3_rx.Init.Request             = DMA_REQUEST_USART3_RX;
    hdma_usart3_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_usart3_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart3_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart3_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart3_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart3_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_usart3_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_usart3_rx) != HAL_OK) {
        return;
    }

    __HAL_LINKDMA(huart, hdmarx, hdma_usart3_rx);

    /* The register version enabled HTIE/TCIE on the channel but never
     * enabled DMA1_Channel2_IRQn in the NVIC, so those interrupts never
     * reached the core and the buffer was only drained on IDLE. See the
     * note in the header comment of usart3.h. */
    HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
    HAL_NVIC_SetPriority(USART3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

void USART3_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3) {
        return;
    }

    __HAL_RCC_USART3_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_4 | GPIO_PIN_5);
    HAL_DMA_DeInit(&hdma_usart3_rx);
    HAL_NVIC_DisableIRQ(DMA1_Channel2_IRQn);
    HAL_NVIC_DisableIRQ(USART3_IRQn);
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void USART3_Config(void)
{
    huart3.Instance                    = USART3;
    huart3.Init.BaudRate               = USART3_BAUDRATE;
    huart3.Init.WordLength             = UART_WORDLENGTH_8B;
    huart3.Init.StopBits               = UART_STOPBITS_1;
    huart3.Init.Parity                 = UART_PARITY_NONE;
    huart3.Init.Mode                   = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart3.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart3.Init.ClockPrescaler         = UART_PRESCALER_DIV1;   /* PRESC = 0 */
    huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart3) != HAL_OK) {
        return;
    }

    /* CR1.FIFOEN plus thresholds. At 115200 the FIFO buys real headroom
     * against interrupt latency. */
    (void)HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8);
    (void)HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8);
    (void)HAL_UARTEx_EnableFifoMode(&huart3);

    usart3_restart_rx();
}

void USART3_Transmit(const uint8_t *data, uint16_t length)
{
    (void)HAL_UART_Transmit(&huart3, (uint8_t *)data, length,
                            USART3_TX_TIMEOUT_MS);
}

void ESP32_SetRx_Callback(void (*rx_cb)(const uint8_t *buf, uint16_t length))
{
    p_rx_cb = rx_cb;
}

/* ------------------------------------------------------------------------- */
/* IRQ handlers                                                              */
/*                                                                           */
/* NOTE: delete these if stm32l4xx_it.c already defines them, and put the    */
/* HAL_*_IRQHandler() calls in the existing versions instead.                */
/* ------------------------------------------------------------------------- */

void USART3_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart3);
}

void DMA1_Channel2_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart3_rx);
}

/* ------------------------------------------------------------------------- */
/* Callbacks - routed here by the dispatcher in system.c                     */
/* ------------------------------------------------------------------------- */

void USART3_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance != USART3) {
        return;
    }

    esp32_collect(Size);

    /* Only an idle line ends a frame; HT/TC events just drain the circular
     * buffer into esp32_rx_buffer. */
    if (HAL_UARTEx_GetRxEventType(huart) != HAL_UART_RXEVENT_IDLE) {
        return;
    }

    if (esp32_rx_length > 0U) {
        if (!esp32_rx_overflow && (p_rx_cb != NULL)) {
            p_rx_cb(esp32_rx_buffer, esp32_rx_length);
        }
        esp32_rx_length   = 0U;
        esp32_rx_overflow = 0U;
    }
}

void USART3_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3) {
        return;
    }

    /* HAL has already cleared ORE/NE/FE/PE. Restart only if reception
     * actually stopped. */
    if (HAL_UART_GetState(huart) != HAL_UART_STATE_BUSY_RX) {
        usart3_restart_rx();
    }
}

/* ------------------------------------------------------------------------- */
/* Internals                                                                 */
/* ------------------------------------------------------------------------- */

static void usart3_restart_rx(void)
{
    esp32_rx_old_pos  = 0U;
    esp32_rx_length   = 0U;
    esp32_rx_overflow = 0U;

    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart3, usart3_rx_buffer,
                                       USART3_RX_BUFFER_SIZE);
}

/* Copy everything the DMA wrote since the previous event, handling the wrap.
 * Same algorithm as ESP32_UART_DMA_Rx(), but the position comes from HAL
 * instead of being read out of CNDTR. */
static void esp32_collect(uint16_t curr_pos)
{
    if (curr_pos == esp32_rx_old_pos) {
        return;
    }

    if (curr_pos > esp32_rx_old_pos) {
        /* Linear: [ --- R DDDD W --- ] */
        uint16_t length = curr_pos - esp32_rx_old_pos;

        if ((esp32_rx_length + length) <= ESP32_RX_BUFFER_SIZE) {
            memcpy(&esp32_rx_buffer[esp32_rx_length],
                   &usart3_rx_buffer[esp32_rx_old_pos], length);
            esp32_rx_length += length;
        } else {
            esp32_rx_overflow = 1U;
        }
    } else {
        /* Wrapped: [ DDD W ----- R DDD ] */
        uint16_t length_to_end = USART3_RX_BUFFER_SIZE - esp32_rx_old_pos;

        if ((esp32_rx_length + length_to_end + curr_pos) <= ESP32_RX_BUFFER_SIZE) {
            if (length_to_end > 0U) {
                memcpy(&esp32_rx_buffer[esp32_rx_length],
                       &usart3_rx_buffer[esp32_rx_old_pos], length_to_end);
                esp32_rx_length += length_to_end;
            }
            if (curr_pos > 0U) {
                memcpy(&esp32_rx_buffer[esp32_rx_length],
                       &usart3_rx_buffer[0], curr_pos);
                esp32_rx_length += curr_pos;
            }
        } else {
            esp32_rx_overflow = 1U;
        }
    }

    esp32_rx_old_pos = curr_pos;
}