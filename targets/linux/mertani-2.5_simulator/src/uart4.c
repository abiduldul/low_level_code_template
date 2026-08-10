#include "main.h"
#include "UartProcess.h"

#define DMAMUX_REQ_UART4_RX  31U
#define DMAMUX_REQ_UART4_TX  32U

static UartProcess_t Uart4Process;

void uart4_transmit(const uint8_t *data, uint16_t length);

void UART4_Config() {
    RCC->APB1ENR1 |= RCC_APB1ENR1_UART4EN;
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->AHB1ENR |= (RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMA2EN | RCC_AHB1ENR_DMAMUX1EN);
    // ---------------------------------------------------------
    // UART4: PA0 (TX, AF8), PA1 (RX, AF8)
    // ---------------------------------------------------------
    GPIOA->BSRR |= GPIO_BSRR_BR8;

    GPIOA->MODER &= ~(GPIO_MODER_MODE8);
    GPIOA->MODER |= (GPIO_MODER_MODE8_0); // Output mode

    GPIOA->MODER &= ~(GPIO_MODER_MODE0 | GPIO_MODER_MODE1);
    GPIOA->MODER |= (2 << GPIO_MODER_MODE0_Pos) | (2 << GPIO_MODER_MODE1_Pos); // AF Mode
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL0 | GPIO_AFRL_AFSEL1);
    GPIOA->AFR[0] |= (8 << GPIO_AFRL_AFSEL0_Pos) | (8 << GPIO_AFRL_AFSEL1_Pos); // AF8
    // ---------------------------------------------------------
    // UART4 - 115200 Baud
    // Formula: BRR = UART_CLK / Baudrate
    // ---------------------------------------------------------
    
    DMA2_Channel1->CCR = 0;
    DMA2_Channel1->CCR |= (DMA_CCR_MINC | DMA_CCR_CIRC);
    DMA2_Channel1->CPAR = (uint32_t)&UART4->RDR;
    DMA2_Channel1->CMAR = (uint32_t)Uart4Process.uart_rx_buffer;
    DMA2_Channel1->CNDTR = UART_BUFFER_SIZE;

    DMAMUX1_Channel7->CCR = DMAMUX_REQ_UART4_RX;
    DMA2_Channel1->CCR |= DMA_CCR_EN;

    DMA1_Channel5->CCR = 0;
    DMA1_Channel5->CCR |= (DMA_CCR_DIR | DMA_CCR_MINC);
    DMA1_Channel5->CPAR = (uint32_t)&UART4->TDR;
    DMAMUX1_Channel4->CCR = DMAMUX_REQ_UART4_TX;
    
    UART4->CR1 &= ~USART_CR1_UE;

    UART4->CR1 = 0;
    UART4->CR2 = 0;
    UART4->CR3 = 0;
    UART4->PRESC = 0;

    UART4->BRR = PCLK / 115200;

    UART4->ICR |= USART_ICR_IDLECF;

    UART4->CR1 |= (USART_CR1_IDLEIE | USART_CR1_FIFOEN | 
        USART_CR1_TE | USART_CR1_RE | USART_CR1_PEIE);

    UART4->CR3 |= (USART_CR3_DMAR | USART_CR3_EIE);

    UART4->CR1 |= USART_CR1_UE;  // Enable

    NVIC_EnableIRQ(UART4_IRQn);

    UartProcess_Init(&Uart4Process, '2', UART4, DMA2_Channel1, uart4_transmit);

    return;
}

void UART4_IRQHandler() {
    UartProcess_Callback(&Uart4Process);

    /*uint32_t isr_flags = UART4->ISR;

    if(isr_flags & (USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE | USART_ISR_PE)) {
        UART4->ICR |= (USART_ICR_ORECF | USART_ICR_NECF | USART_ICR_FECF | USART_ICR_PECF);
    }*/

    return;
}

void uart4_transmit(const uint8_t *data, uint16_t length) {
    // 1. Wait for previous transfer to finish (skip on very first call when CNDTR=0)
    if (DMA1_Channel5->CNDTR > 0) {
        while (!(DMA1->ISR & DMA_ISR_TCIF5));
    }

    // 2. Disable DMAT and DMA channel before reconfiguring
    UART4->CR3 &= ~USART_CR3_DMAT;
    DMA1_Channel5->CCR &= ~DMA_CCR_EN;
    while (DMA1_Channel5->CCR & DMA_CCR_EN);

    // 3. Clear all DMA Channel 2 flags
    DMA1->IFCR = DMA_IFCR_CGIF5;

    // 4. Clear UART TX/error flags
    UART4->ICR = USART_ICR_TCCF | USART_ICR_ORECF | USART_ICR_FECF;

    // 5. Load buffer and length
    DMA1_Channel5->CMAR  = (uint32_t)data;
    DMA1_Channel5->CNDTR = length;

    // 6. Enable DMA then DMAT
    DMA1_Channel5->CCR |= DMA_CCR_EN;
    UART4->CR3       |= USART_CR3_DMAT;

    return;
}