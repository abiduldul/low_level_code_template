#include "stm32l4p5xx.h"
#include "system.h"

#include <stdint.h>
#include <string.h>

#define USART1_RX_BUFFER_SIZE    64
#define RS485_RX_BUFFER_SIZE    256

#define DMAMUX_REQ_USART1_RX    25U
#define DMAMUX_REQ_USART1_TX    26U

void (*p_rx_cb)(const uint8_t* buffer, uint16_t length);

static uint8_t usart1_rx_buffer[USART1_RX_BUFFER_SIZE];

static uint8_t rs485_rx_buffer[RS485_RX_BUFFER_SIZE];
static uint16_t rs485_rx_length;

static void RS485_UART_DMA_Rx(void);
static void usart1_transmit(const uint8_t *data, uint16_t length);

void RS485_Init() {
    RCC->AHB2ENR |= (RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN);
    //RCC->AHB1ENR |= (RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMA2EN | RCC_AHB1ENR_DMAMUX1EN);
    RCC->AHB1ENR |= (RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMAMUX1EN);
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
    DMA1_Channel3->CCR |= (DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE);
    DMA1_Channel3->CPAR = (uint32_t)&USART1->RDR;
    DMA1_Channel3->CMAR = (uint32_t)usart1_rx_buffer;
    DMA1_Channel3->CNDTR = USART1_RX_BUFFER_SIZE;

    DMAMUX1_Channel2->CCR = DMAMUX_REQ_USART1_RX;
    DMA1_Channel3->CCR |= DMA_CCR_EN;

    /*DMA2_Channel5->CCR = 0;
    DMA2_Channel5->CCR |= (DMA_CCR_DIR | DMA_CCR_MINC);
    DMA2_Channel5->CPAR = (uint32_t)&USART1->TDR;
    DMAMUX1_Channel11->CCR = DMAMUX_REQ_USART1_TX;*/

    // ---------------------------------------------------------
    // USART1 - 9600 Baud, RS485 Hardware Flow Control (DE)
    // ---------------------------------------------------------
    USART1->CR1 &= ~USART_CR1_UE;

    USART1->CR1 = 0;
    USART1->CR2 = 0;
    USART1->CR3 = 0;
    USART1->PRESC = 0;

    USART1->BRR =  System_GetClockFreq() / 9600;
    USART1->CR3 |= USART_CR3_DEM; // Enable Driver Enable (RS485 mode)
    // Note: DE Polarity is High by default. If you need it active low, set USART_CR3_DEP bit.
    USART1->CR1 |= (USART_CR1_TE | USART_CR1_RE);

    USART1->ICR |= USART_ICR_IDLECF;

    USART1->CR1 |= (USART_CR1_IDLEIE | USART_CR1_FIFOEN | 
        USART_CR1_TE | USART_CR1_RE | USART_CR1_PEIE);

    USART1->CR3 |= (USART_CR3_DMAR | USART_CR3_EIE);

    USART1->CR1 |= USART_CR1_UE;  // Enable
    NVIC_EnableIRQ(USART1_IRQn);
    NVIC_EnableIRQ(DMA1_Channel3_IRQn);

    return;
}
void RS485_Send(const uint8_t* buf, uint16_t length) {
    usart1_transmit(buf, length);

    return;
}
void RS485_SetRx_Callback(void (*cb)(const uint8_t* buffer, uint16_t length)) {
    p_rx_cb = cb;
    return;
}

void DMA1_Channel3_IRQHandler() {
    if(DMA1->ISR & DMA_ISR_HTIF3) {
        DMA1->IFCR = DMA_IFCR_CHTIF3;

        RS485_UART_DMA_Rx();
    }
    
    if(DMA1->ISR & DMA_ISR_TCIF3) {
        DMA1->IFCR = DMA_IFCR_CTCIF3;

        RS485_UART_DMA_Rx();
    }
}

void USART1_IRQHandler() {
    uint32_t isr = USART1->ISR;

    if(isr & USART_ISR_IDLE) {
        USART1->ICR = USART_ICR_IDLECF; // Clear IDLE flag

        RS485_UART_DMA_Rx();

        if(rs485_rx_length > 0) {
            if(p_rx_cb != (void*)0) p_rx_cb(rs485_rx_buffer, rs485_rx_length);

            rs485_rx_length = 0;
        }
    }

    if(isr & (USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE | USART_ISR_PE)) {
        USART1->ICR |= (USART_ICR_ORECF | USART_ICR_NECF | USART_ICR_FECF | USART_ICR_PECF);
    }

    return;
}

static void usart1_transmit(const uint8_t *data, uint16_t length) {
    // 1. Wait for previous transfer to finish (skip on very first call when CNDTR=0)
/*    if (DMA2_Channel5->CNDTR > 0) {
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
    USART1->CR3       |= USART_CR3_DMAT;*/

    while(length--) {
        while(!(USART1->ISR & USART_ISR_TXE_TXFNF));

        USART1->TDR = *(data++);
    }

    return;
}

// This function is called by your Interrupt Service Routines
static void RS485_UART_DMA_Rx(void) {
    // Keep track of where we left off last time
    static uint16_t old_pos = 0;
    
    // Calculate current write position of the DMA
    uint16_t curr_pos = USART1_RX_BUFFER_SIZE - DMA1_Channel3->CNDTR;
    
    // If there is no new data, exit immediately
    if (curr_pos == old_pos) {
        return; 
    }

    if (curr_pos > old_pos) {
        // Case 1: Linear data. The write pointer is ahead of the read pointer.
        // [ --- RDDDDDDW--- ] (R = Read, D = Data, W = Write)
        uint16_t length = curr_pos - old_pos;
        memcpy(&rs485_rx_buffer[rs485_rx_length], &usart1_rx_buffer[old_pos], length);
        rs485_rx_length += length;
    } 
    else {
        // Case 2: Wrap-around data. The write pointer looped behind the read pointer.
        // [ DDDW-------RDDD ] 
        
        // Part A: Read from the old position to the very end of the buffer
        uint16_t length_to_end = USART1_RX_BUFFER_SIZE - old_pos;
        memcpy(&rs485_rx_buffer[rs485_rx_length], &usart1_rx_buffer[old_pos], length_to_end);
        rs485_rx_length += length_to_end;
        
        // Part B: Read from the beginning of the buffer up to the current position
        if (curr_pos > 0) {
            memcpy(&rs485_rx_buffer[rs485_rx_length], &usart1_rx_buffer[0], curr_pos);
            rs485_rx_length += curr_pos;
        }
    }
    
    // Update the read pointer for the next interrupt
    old_pos = curr_pos;
}