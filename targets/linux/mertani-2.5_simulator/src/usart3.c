#include "main.h"
#include "UartProcess.h"

#define DMAMUX_REQ_USART3_RX  29U
#define DMAMUX_REQ_USART3_TX  30U

static UartProcess_t Uart3Process;

void USART3_Config() {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART3EN;
    RCC->AHB1ENR |= (RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMA2EN | RCC_AHB1ENR_DMAMUX1EN);
    // ---------------------------------------------------------
    // USART3: PC4 (TX, AF7), PC5 (RX, AF7)
    // ---------------------------------------------------------
    GPIOC->MODER &= ~(GPIO_MODER_MODE4 | GPIO_MODER_MODE5);
    GPIOC->MODER |= (2 << GPIO_MODER_MODE4_Pos) | (2 << GPIO_MODER_MODE5_Pos); // AF Mode
    GPIOC->AFR[0] &= ~(GPIO_AFRL_AFSEL4 | GPIO_AFRL_AFSEL5);
    GPIOC->AFR[0] |= (7 << GPIO_AFRL_AFSEL4_Pos) | (7 << GPIO_AFRL_AFSEL5_Pos); // AF7
    
    GPIOC->OSPEEDR |= (GPIO_OSPEEDR_OSPEED4 | GPIO_OSPEEDR_OSPEED5);

    // ---------------------------------------------------------
    // DMA Initialization
    // ---------------------------------------------------------
    DMA1_Channel2->CCR = 0;
    DMA1_Channel2->CCR |= (DMA_CCR_MINC | DMA_CCR_CIRC);
    DMA1_Channel2->CPAR = (uint32_t)&USART3->RDR;
    DMA1_Channel2->CMAR = (uint32_t)Uart3Process.uart_rx_buffer;
    DMA1_Channel2->CNDTR = UART_BUFFER_SIZE;

    DMAMUX1_Channel1->CCR = DMAMUX_REQ_USART3_RX;
    DMA1_Channel2->CCR |= DMA_CCR_EN;

    DMA2_Channel4->CCR = 0;
    DMA2_Channel4->CCR |= (DMA_CCR_DIR | DMA_CCR_MINC);
    DMA2_Channel4->CPAR = (uint32_t)&USART3->TDR;
    DMAMUX1_Channel10->CCR = DMAMUX_REQ_USART3_TX;
    // ---------------------------------------------------------
    // USART3 - 115200 Baud
    // ---------------------------------------------------------
    USART3->CR1 &= ~USART_CR1_UE;

    USART3->CR1 = 0;
    USART3->CR2 = 0;
    USART3->CR3 = 0;
    USART3->PRESC = 0;

    USART3->BRR = PCLK / 115200;
    USART3->CR1 |= (USART_CR1_TE | USART_CR1_RE);

    USART3->ICR |= USART_ICR_IDLECF;
    USART3->CR1 |= (USART_CR1_IDLEIE | USART_CR1_FIFOEN | 
        USART_CR1_TE | USART_CR1_RE | USART_CR1_PEIE);

    USART3->CR3 |= (USART_CR3_DMAR | USART_CR3_EIE);

    USART3->CR1 |= USART_CR1_UE;  // Enable

    NVIC_EnableIRQ(USART3_IRQn);

    UartProcess_Init(&Uart3Process, '1', USART3, DMA1_Channel2, usart3_transmit);

    return;
}

void USART3_IRQHandler() {
    UartProcess_Callback(&Uart3Process);

    return;
}

void usart3_transmit(const uint8_t *data, uint16_t length) {
    // 1. Wait for previous transfer to finish (skip on very first call when CNDTR=0)
    if (DMA2_Channel4->CNDTR > 0) {
        while (!(DMA2->ISR & DMA_ISR_TCIF4));
    }

    // 2. Disable DMAT and DMA channel before reconfiguring
    USART3->CR3 &= ~USART_CR3_DMAT;
    DMA2_Channel4->CCR &= ~DMA_CCR_EN;
    while (DMA2_Channel4->CCR & DMA_CCR_EN);

    // 3. Clear all DMA Channel 2 flags
    DMA2->IFCR = DMA_IFCR_CGIF4;

    // 4. Clear UART TX/error flags
    USART3->ICR = USART_ICR_TCCF | USART_ICR_ORECF | USART_ICR_FECF;

    // 5. Load buffer and length
    DMA2_Channel4->CMAR  = (uint32_t)data;
    DMA2_Channel4->CNDTR = length;

    // 6. Enable DMA then DMAT
    DMA2_Channel4->CCR |= DMA_CCR_EN;
    USART3->CR3       |= USART_CR3_DMAT;

    return;
}