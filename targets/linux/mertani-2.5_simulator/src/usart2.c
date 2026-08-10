#include "main.h"

#include "UartProcess.h"

#define DMAMUX_REQ_USART2_RX  27U
#define DMAMUX_REQ_USART2_TX  28U

static UartProcess_t Uart2Process;
void usart2_transmit(const uint8_t *data, uint16_t length);

void USART2_Config() {
    RCC->AHB2ENR |= (RCC_AHB2ENR_GPIOAEN);
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    RCC->AHB1ENR |= (RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMA2EN | RCC_AHB1ENR_DMAMUX1EN);
    // ---------------------------------------------------------
    // USART2: PA2 (TX, AF7), PA3 (RX, AF7)
    // ---------------------------------------------------------
    GPIOA->MODER &= ~(GPIO_MODER_MODE2 | GPIO_MODER_MODE3);
    GPIOA->MODER |= (2 << GPIO_MODER_MODE2_Pos) | (2 << GPIO_MODER_MODE3_Pos); // AF Mode
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL2 | GPIO_AFRL_AFSEL3);
    GPIOA->AFR[0] |= (7 << GPIO_AFRL_AFSEL2_Pos) | (7 << GPIO_AFRL_AFSEL3_Pos); // AF7

    DMA2_Channel2->CCR = 0;
    DMA2_Channel2->CCR |= (DMA_CCR_MINC | DMA_CCR_CIRC);
    DMA2_Channel2->CPAR = (uint32_t)&USART2->RDR;
    DMA2_Channel2->CMAR = (uint32_t)Uart2Process.uart_rx_buffer;
    DMA2_Channel2->CNDTR = UART_BUFFER_SIZE;

    DMAMUX1_Channel8->CCR = DMAMUX_REQ_USART2_RX;
    DMA2_Channel2->CCR |= DMA_CCR_EN;

    DMA1_Channel4->CCR = 0;
    DMA1_Channel4->CCR |= (DMA_CCR_DIR | DMA_CCR_MINC);
    DMA1_Channel4->CPAR = (uint32_t)&USART2->TDR;
    DMAMUX1_Channel7->CCR = DMAMUX_REQ_USART2_TX;
    // ---------------------------------------------------------
    // USART2 - 115200 Baud
    // ---------------------------------------------------------
    USART2->CR1 &= ~USART_CR1_UE;

    USART2->CR1 = 0;
    USART2->CR2 = 0;
    USART2->CR3 = 0;
    USART2->PRESC = 0;

    USART2->BRR = PCLK / 115200;

    USART2->ICR |= USART_ICR_IDLECF;
    USART2->CR1 |= (USART_CR1_IDLEIE | USART_CR1_FIFOEN | 
        USART_CR1_TE | USART_CR1_RE | USART_CR1_PEIE);

    USART2->CR3 |= (USART_CR3_DMAR | USART_CR3_EIE);

    USART2->CR1 |= USART_CR1_UE;  // Enable

    NVIC_EnableIRQ(USART2_IRQn);

    UartProcess_Init(&Uart2Process, '4', USART2, DMA2_Channel2, usart2_transmit);

    return;
}

void USART2_IRQHandler() {
    UartProcess_Callback(&Uart2Process);

    return;
}

void usart2_transmit(const uint8_t *data, uint16_t length) {
    // 1. Wait for previous transfer to finish (skip on very first call when CNDTR=0)
    if (DMA1_Channel4->CNDTR > 0) {
        while (!(DMA1->ISR & DMA_ISR_TCIF4));
    }

    // 2. Disable DMAT and DMA channel before reconfiguring
    USART2->CR3 &= ~USART_CR3_DMAT;
    DMA1_Channel4->CCR &= ~DMA_CCR_EN;
    while (DMA1_Channel4->CCR & DMA_CCR_EN);

    // 3. Clear all DMA Channel 2 flags
    DMA1->IFCR = DMA_IFCR_CGIF4;

    // 4. Clear UART TX/error flags
    USART2->ICR = USART_ICR_TCCF | USART_ICR_ORECF | USART_ICR_FECF;

    // 5. Load buffer and length
    DMA1_Channel4->CMAR  = (uint32_t)data;
    DMA1_Channel4->CNDTR = length;

    // 6. Enable DMA then DMAT
    DMA1_Channel4->CCR |= DMA_CCR_EN;
    USART2->CR3       |= USART_CR3_DMAT;

    return;
}