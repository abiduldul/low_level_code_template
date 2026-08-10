#include "main.h"

#include "../CircularBuffer/CircularBuffer.h"

#define RX_BUFFER_SIZE  16384

#define DMAMUX_REQ_LPUART1_RX  35U
#define DMAMUX_REQ_LPUART1_TX  36U

static uint8_t lpuart1_rx_buffer[RX_BUFFER_SIZE];
static uint8_t lpuart1_rx_circularbuffer[RX_BUFFER_SIZE];

static uint16_t rx_read_pos;

static CircularBuffer_t cbLPUART1;

void (*lpuart1_callback)(const uint8_t*, uint16_t);

void lpuart1_set_cb(void (*lpuart1_cb)(const uint8_t*, uint16_t)) {
    lpuart1_callback = lpuart1_cb;

    return;
}

void LPUART1_Config() {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    RCC->APB1ENR2 |= RCC_APB1ENR2_LPUART1EN;
    RCC->AHB1ENR |= (RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMA2EN | RCC_AHB1ENR_DMAMUX1EN);
    // ---------------------------------------------------------
    // LPUART1: PC1 (TX, AF8), PC0 (RX, AF8)
    // ---------------------------------------------------------
    GPIOC->MODER &= ~(GPIO_MODER_MODE0 | GPIO_MODER_MODE1);
    GPIOC->MODER |= (2 << GPIO_MODER_MODE0_Pos) | (2 << GPIO_MODER_MODE1_Pos); // AF Mode
    GPIOC->AFR[0] &= ~(GPIO_AFRL_AFSEL0 | GPIO_AFRL_AFSEL1);
    GPIOC->AFR[0] |= (8 << GPIO_AFRL_AFSEL0_Pos) | (8 << GPIO_AFRL_AFSEL1_Pos); // AF8

    GPIOC->OSPEEDR |= (GPIO_OSPEEDR_OSPEED0 | GPIO_OSPEEDR_OSPEED1);

    DMA1_Channel1->CCR = 0;
    DMA1_Channel1->CCR |= (DMA_CCR_MINC | DMA_CCR_CIRC);
    DMA1_Channel1->CPAR = (uint32_t)&LPUART1->RDR;
    DMA1_Channel1->CMAR = (uint32_t)lpuart1_rx_buffer;
    DMA1_Channel1->CNDTR = sizeof(lpuart1_rx_buffer);

    DMAMUX1_Channel0->CCR = DMAMUX_REQ_LPUART1_RX & DMAMUX_CxCR_DMAREQ_ID;
    DMA1_Channel1->CCR |= DMA_CCR_EN;

    DMA2_Channel3->CCR = 0;
    DMA2_Channel3->CCR |= (DMA_CCR_DIR | DMA_CCR_MINC);
    DMA2_Channel3->CPAR = (uint32_t)&LPUART1->TDR;
    DMAMUX1_Channel9->CCR = DMAMUX_REQ_LPUART1_TX & DMAMUX_CxCR_DMAREQ_ID;

    // ---------------------------------------------------------
    // LPUART1 - 921600 Baud
    // Formula: BRR = (256 * LPUART_CLK) / Baudrate
    // ---------------------------------------------------------
    LPUART1->CR1 &= ~USART_CR1_UE; // Disable UART to configure
    
    LPUART1->CR1 = 0;
    LPUART1->CR2 = 0;
    LPUART1->CR3 = 0;
    LPUART1->PRESC = 0;
    
    LPUART1->BRR = (uint32_t)((256ULL * PCLK) / 921600);

    LPUART1->ICR |= USART_ICR_IDLECF;

    LPUART1->CR1 |= (USART_CR1_IDLEIE | USART_CR1_FIFOEN | 
        USART_CR1_TE | USART_CR1_RE | USART_CR1_PEIE);

    LPUART1->CR3 |= (USART_CR3_DMAR | USART_CR3_EIE);

    LPUART1->CR1 |= USART_CR1_UE;  // Enable

    NVIC_EnableIRQ(LPUART1_IRQn);

    CircularBuffer_Init(&cbLPUART1, lpuart1_rx_circularbuffer, RX_BUFFER_SIZE);
}

void lpuart1_rx_loop() {
    static uint8_t buffer_process[RX_BUFFER_SIZE];

    uint16_t length;

    if((length = CircularBuffer_Pop(&cbLPUART1, buffer_process))) {
        if(lpuart1_callback != NULL) lpuart1_callback(buffer_process, length);
    }

    return;
}

void LPUART1_IRQHandler() {
    if(LPUART1->ISR & USART_ISR_IDLE) {
        LPUART1->ICR |= USART_ICR_IDLECF;

        uint16_t curr_write_pos = RX_BUFFER_SIZE - DMA1_Channel1->CNDTR;

        if(curr_write_pos >= RX_BUFFER_SIZE) {
            curr_write_pos = 0;
        }

        if(curr_write_pos >= rx_read_pos) {
            uint16_t bytes_received = curr_write_pos - rx_read_pos;

            if(bytes_received > 0) {
                CircularBuffer_Push(&cbLPUART1, lpuart1_rx_buffer + rx_read_pos, bytes_received);
            }
        } else {
            uint16_t bytes_end = RX_BUFFER_SIZE - rx_read_pos;
            uint16_t bytes_head = curr_write_pos;

            CircularBuffer_Push(&cbLPUART1, lpuart1_rx_buffer + rx_read_pos, bytes_end);
            
            if(bytes_head > 0) {
                CircularBuffer_Push(&cbLPUART1, lpuart1_rx_buffer, bytes_head);
            }
        }

        rx_read_pos = curr_write_pos;
    }

    return;
}

void lpuart1_transmit(const uint8_t* data, uint16_t length) {
    while(length--) {
        while(!(LPUART1->ISR & USART_ISR_TXE_TXFNF));

        LPUART1->TDR = *(data++);
    }
}