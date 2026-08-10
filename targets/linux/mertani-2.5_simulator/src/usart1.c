#include "main.h"

#include "UartProcess.h"

#define DMAMUX_REQ_USART1_RX    25U
#define DMAMUX_REQ_USART1_TX    26U

void usart1_transmit(const uint8_t *data, uint16_t length);

static UartProcess_t Uart1Process;

void USART1_Config() {
    RCC->AHB2ENR |= (RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN);
    RCC->AHB1ENR |= (RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMA2EN | RCC_AHB1ENR_DMAMUX1EN);
    RCC->APB2ENR  |= RCC_APB2ENR_USART1EN;
    // ---------------------------------------------------------
    // USART1: PA9 (TX, AF7), PA10 (RX, AF7), PB3 (DE, AF7)
    // ---------------------------------------------------------
    GPIOA->MODER &= ~(GPIO_MODER_MODE9 | GPIO_MODER_MODE10);
    GPIOA->MODER |= (2 << GPIO_MODER_MODE9_Pos) | (2 << GPIO_MODER_MODE10_Pos); // AF Mode
    GPIOA->AFR[1] &= ~(GPIO_AFRH_AFSEL9 | GPIO_AFRH_AFSEL10);
    GPIOA->AFR[1] |= (7 << GPIO_AFRH_AFSEL9_Pos) | (7 << GPIO_AFRH_AFSEL10_Pos); // AF7

    GPIOB->MODER &= ~GPIO_MODER_MODE3;
    GPIOB->MODER |= (2 << GPIO_MODER_MODE3_Pos); // AF Mode
    GPIOB->AFR[0] &= ~GPIO_AFRL_AFSEL3;
    GPIOB->AFR[0] |= (7 << GPIO_AFRL_AFSEL3_Pos); // AF7

    DMA1_Channel3->CCR = 0;
    DMA1_Channel3->CCR |= (DMA_CCR_MINC | DMA_CCR_CIRC);
    DMA1_Channel3->CPAR = (uint32_t)&USART1->RDR;
    DMA1_Channel3->CMAR = (uint32_t)Uart1Process.uart_rx_buffer;
    DMA1_Channel3->CNDTR = UART_BUFFER_SIZE;

    DMAMUX1_Channel2->CCR = DMAMUX_REQ_USART1_RX;
    DMA1_Channel3->CCR |= DMA_CCR_EN;

    DMA2_Channel5->CCR = 0;
    DMA2_Channel5->CCR |= (DMA_CCR_DIR | DMA_CCR_MINC);
    DMA2_Channel5->CPAR = (uint32_t)&USART1->TDR;
    DMAMUX1_Channel11->CCR = DMAMUX_REQ_USART1_TX;

    // ---------------------------------------------------------
    // USART1 - 9600 Baud, RS485 Hardware Flow Control (DE)
    // ---------------------------------------------------------
    USART1->CR1 &= ~USART_CR1_UE;

    USART1->CR1 = 0;
    USART1->CR2 = 0;
    USART1->CR3 = 0;
    USART1->PRESC = 0;

    USART1->BRR = PCLK / 9600;
    USART1->CR3 |= USART_CR3_DEM; // Enable Driver Enable (RS485 mode)
    // Note: DE Polarity is High by default. If you need it active low, set USART_CR3_DEP bit.
    USART1->CR1 |= (USART_CR1_TE | USART_CR1_RE);

    USART1->ICR |= USART_ICR_IDLECF;

    USART1->CR1 |= (USART_CR1_IDLEIE | USART_CR1_FIFOEN | 
        USART_CR1_TE | USART_CR1_RE | USART_CR1_PEIE);

    USART1->CR3 |= (USART_CR3_DMAR | USART_CR3_EIE);

    USART1->CR1 |= USART_CR1_UE;  // Enable
    NVIC_EnableIRQ(USART1_IRQn);

    UartProcess_Init(&Uart1Process, '3', USART1, DMA1_Channel3, usart1_transmit);

    return;
}

void USART1_IRQHandler() {
    UartProcess_Callback(&Uart1Process);

    return;
}

void usart1_transmit(const uint8_t *data, uint16_t length) {
    // 1. Wait for previous transfer to finish (skip on very first call when CNDTR=0)
    if (DMA2_Channel5->CNDTR > 0) {
        while (!(DMA2->ISR & DMA_ISR_TCIF5));
    }

    // 2. Disable DMAT and DMA channel before reconfiguring
    USART1->CR3 &= ~USART_CR3_DMAT;
    DMA2_Channel5->CCR &= ~DMA_CCR_EN;
    while (DMA2_Channel5->CCR & DMA_CCR_EN);

    // 3. Clear all DMA Channel 2 flags
    DMA2->IFCR = DMA_IFCR_CGIF5;

    // 4. Clear UART TX/error flags
    USART1->ICR = USART_ICR_TCCF | USART_ICR_ORECF | USART_ICR_FECF;

    // 5. Load buffer and length
    DMA2_Channel5->CMAR  = (uint32_t)data;
    DMA2_Channel5->CNDTR = length;

    // 6. Enable DMA then DMAT
    DMA2_Channel5->CCR |= DMA_CCR_EN;
    USART1->CR3       |= USART_CR3_DMAT;

    return;
}