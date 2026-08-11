#include "uart4.h"

#include "stm32l4xx_hal.h"

#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */

#define UART4_RX_BUFFER_SIZE    256U    /* circular DMA landing buffer */

#define UART4_BAUDRATE          115200U
#define UART4_TX_TIMEOUT_MS     500U

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

static UART_HandleTypeDef huart4;
static DMA_HandleTypeDef  hdma_uart4_rx;

static uint8_t  uart4_rx_buffer[UART4_RX_BUFFER_SIZE];
static uint16_t uart4_rx_old_pos;

static void (*p_rx_cb)(const uint8_t *buf, uint16_t length);

static void uart4_feed(const uint8_t *data, uint16_t length);
static void uart4_collect(uint16_t curr_pos);
static void uart4_restart_rx(void);

/* ------------------------------------------------------------------------- */
/* MSP - clocks, GPIO, DMA, NVIC                                             */
/* ------------------------------------------------------------------------- */

void UART4_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio = {0};

    if (huart->Instance != UART4) {
        return;
    }

    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMAMUX1_CLK_ENABLE();

    /* PA0 = TX, PA1 = RX */
    gpio.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;               /* idle-high line */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* DMA1 Channel 4 <- UART4_RX, circular */
    hdma_uart4_rx.Instance                 = DMA1_Channel4;
    hdma_uart4_rx.Init.Request             = DMA_REQUEST_UART4_RX;
    hdma_uart4_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_uart4_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_uart4_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_uart4_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_uart4_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_uart4_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_uart4_rx.Init.Priority            = DMA_PRIORITY_MEDIUM;

    if (HAL_DMA_Init(&hdma_uart4_rx) != HAL_OK) {
        return;
    }

    __HAL_LINKDMA(huart, hdmarx, hdma_uart4_rx);

    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_SetPriority(UART4_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
}

void UART4_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART4) {
        return;
    }

    __HAL_RCC_UART4_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0 | GPIO_PIN_1);
    HAL_DMA_DeInit(&hdma_uart4_rx);
    HAL_NVIC_DisableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_DisableIRQ(UART4_IRQn);
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void UART4_Config(void)
{
    huart4.Instance                    = UART4;
    huart4.Init.BaudRate               = UART4_BAUDRATE;
    huart4.Init.WordLength             = UART_WORDLENGTH_8B;
    huart4.Init.StopBits               = UART_STOPBITS_1;
    huart4.Init.Parity                 = UART_PARITY_NONE;
    huart4.Init.Mode                   = UART_MODE_TX_RX;
    huart4.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart4.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart4.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart4.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart4) != HAL_OK) {
        return;
    }

    uart4_restart_rx();
}

void UART4_Transmit(const uint8_t *data, uint16_t length)
{
    (void)HAL_UART_Transmit(&huart4, (uint8_t *)data, length,
                            UART4_TX_TIMEOUT_MS);
}

void UART4_SetRx_Callback(void (*rx_cb)(const uint8_t *buf, uint16_t length))
{
    p_rx_cb = rx_cb;
}

/* ------------------------------------------------------------------------- */
/* IRQ handlers                                                              */
/*                                                                           */
/* Delete these if stm32l4xx_it.c already defines them, and put the          */
/* HAL_*_IRQHandler() calls in the existing versions instead.                */
/* ------------------------------------------------------------------------- */

void UART4_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart4);
}

void DMA1_Channel4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_uart4_rx);
}

/* ------------------------------------------------------------------------- */
/* Callbacks - routed here by the dispatcher in system.c                     */
/* ------------------------------------------------------------------------- */

void UART4_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance != UART4) {
        return;
    }

    /* Half-transfer, transfer-complete and idle-line all mean the same
     * thing: the DMA has written up to this position. Drain and forward. */
    uart4_collect(Size);
}

void UART4_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART4) {
        return;
    }

    if (HAL_UART_GetState(huart) != HAL_UART_STATE_BUSY_RX) {
        uart4_restart_rx();
    }
}

/* ------------------------------------------------------------------------- */
/* Internals                                                                 */
/* ------------------------------------------------------------------------- */

static void uart4_restart_rx(void)
{
    uart4_rx_old_pos = 0U;

    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart4, uart4_rx_buffer,
                                       UART4_RX_BUFFER_SIZE);
}

static void uart4_feed(const uint8_t *data, uint16_t length)
{
    if ((length > 0U) && (p_rx_cb != NULL)) {
        p_rx_cb(data, length);
    }
}

/* Forward everything the DMA wrote since the previous event, handling the
 * wrap. No copying and no size ceiling. */
static void uart4_collect(uint16_t curr_pos)
{
    if (curr_pos == uart4_rx_old_pos) {
        return;
    }

    if (curr_pos > uart4_rx_old_pos) {
        /* Linear: [ --- R DDDD W --- ] */
        uart4_feed(&uart4_rx_buffer[uart4_rx_old_pos],
                   (uint16_t)(curr_pos - uart4_rx_old_pos));
    } else {
        /* Wrap: [ DDD W ----- R DDD ] */
        uart4_feed(&uart4_rx_buffer[uart4_rx_old_pos],
                   (uint16_t)(UART4_RX_BUFFER_SIZE - uart4_rx_old_pos));
        uart4_feed(&uart4_rx_buffer[0], curr_pos);
    }

    /* HAL reports Size == RxXferSize on transfer-complete. As a write
     * position that is the same as 0. */
    uart4_rx_old_pos = (curr_pos >= UART4_RX_BUFFER_SIZE) ? 0U : curr_pos;
}