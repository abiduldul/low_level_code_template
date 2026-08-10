#include "stm32l4p5xx.h"
#include "system.h"

#include <stdint.h>
#include <string.h>

#define DMAMUX_REQ_USART3_RX  29U
#define DMAMUX_REQ_USART3_TX  30U

#define USART3_RX_BUFFER_SIZE    512
#define ESP32_RX_BUFFER_SIZE    2048

static uint8_t usart3_rx_buffer[USART3_RX_BUFFER_SIZE];

static uint8_t esp32_rx_buffer[ESP32_RX_BUFFER_SIZE];
static uint16_t esp32_rx_length;

static void (*p_rx_cb)(const uint8_t* buf, uint16_t length);

static void ESP32_UART_DMA_Rx(void);

void USART3_Config() {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART3EN;
    //RCC->AHB1ENR |= (RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMA2EN | RCC_AHB1ENR_DMAMUX1EN);
    RCC->AHB1ENR |= (RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMAMUX1EN);
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
    DMA1_Channel2->CCR |= (DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_HTIE | DMA_CCR_TCIE);
    DMA1_Channel2->CPAR = (uint32_t)&USART3->RDR;
    DMA1_Channel2->CMAR = (uint32_t)usart3_rx_buffer;
    DMA1_Channel2->CNDTR = USART3_RX_BUFFER_SIZE;

    DMAMUX1_Channel1->CCR = DMAMUX_REQ_USART3_RX;
    DMA1_Channel2->CCR |= DMA_CCR_EN;

    /*DMA2_Channel4->CCR = 0;
    DMA2_Channel4->CCR |= (DMA_CCR_DIR | DMA_CCR_MINC);
    DMA2_Channel4->CPAR = (uint32_t)&USART3->TDR;
    DMAMUX1_Channel10->CCR = DMAMUX_REQ_USART3_TX;*/
    // ---------------------------------------------------------
    // USART3 - 115200 Baud
    // ---------------------------------------------------------
    USART3->CR1 &= ~USART_CR1_UE;

    USART3->CR1 = 0;
    USART3->CR2 = 0;
    USART3->CR3 = 0;
    USART3->PRESC = 0;

    USART3->BRR = System_GetClockFreq() / 115200;
    USART3->CR1 |= (USART_CR1_TE | USART_CR1_RE);

    USART3->ICR |= USART_ICR_IDLECF;
    USART3->CR1 |= (USART_CR1_IDLEIE | USART_CR1_FIFOEN | 
        USART_CR1_TE | USART_CR1_RE | USART_CR1_PEIE);

    USART3->CR3 |= (USART_CR3_DMAR | USART_CR3_EIE);

    USART3->CR1 |= USART_CR1_UE;  // Enable

    NVIC_EnableIRQ(USART3_IRQn);

    return;
}

void DMA1_Channel2_IRQHandler() {
    if(DMA1->ISR & DMA_ISR_HTIF2) {
        DMA1->IFCR = DMA_IFCR_CHTIF2;

        ESP32_UART_DMA_Rx();
    }
    
    if(DMA1->ISR & DMA_ISR_TCIF2) {
        DMA1->IFCR = DMA_IFCR_CTCIF2;

        ESP32_UART_DMA_Rx();
    }

    return;
}

void USART3_IRQHandler() {
    uint32_t isr = USART3->ISR;

    if(isr & USART_ISR_IDLE) {
        USART3->ICR = USART_ICR_IDLECF; // Clear IDLE flag

        ESP32_UART_DMA_Rx();

        if(esp32_rx_length > 0) {
            if(p_rx_cb != (void*)0) p_rx_cb(esp32_rx_buffer, esp32_rx_length);

            esp32_rx_length = 0;
        }
    }

    if(isr & (USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE | USART_ISR_PE)) {
        USART3->ICR |= (USART_ICR_ORECF | USART_ICR_NECF | USART_ICR_FECF | USART_ICR_PECF);
    }

    return;
}

void ESP32_SetRx_Callback(void (*rx_cb)(const uint8_t* buf, uint16_t length)) {
    p_rx_cb = rx_cb;

    return;
}

// This function is called by your Interrupt Service Routines
static void ESP32_UART_DMA_Rx(void) {
    // Keep track of where we left off last time
    static uint16_t old_pos = 0;
    
    // Calculate current write position of the DMA
    uint16_t curr_pos = USART3_RX_BUFFER_SIZE - DMA1_Channel2->CNDTR;
    
    // If there is no new data, exit immediately
    if (curr_pos == old_pos) {
        return; 
    }

    if (curr_pos > old_pos) {
        // Case 1: Linear data. The write pointer is ahead of the read pointer.
        // [ --- RDDDDDDW--- ] (R = Read, D = Data, W = Write)
        uint16_t length = curr_pos - old_pos;
        memcpy(&esp32_rx_buffer[esp32_rx_length], &usart3_rx_buffer[old_pos], length);
        esp32_rx_length += length;
    } 
    else {
        // Case 2: Wrap-around data. The write pointer looped behind the read pointer.
        // [ DDDW-------RDDD ] 
        
        // Part A: Read from the old position to the very end of the buffer
        uint16_t length_to_end = USART3_RX_BUFFER_SIZE - old_pos;
        memcpy(&esp32_rx_buffer[esp32_rx_length], &usart3_rx_buffer[old_pos], length_to_end);
        esp32_rx_length += length_to_end;
        
        // Part B: Read from the beginning of the buffer up to the current position
        if (curr_pos > 0) {
            memcpy(&esp32_rx_buffer[esp32_rx_length], &usart3_rx_buffer[0], curr_pos);
            esp32_rx_length += curr_pos;
        }
    }
    
    // Update the read pointer for the next interrupt
    old_pos = curr_pos;
}

void USART3_Transmit(const uint8_t *data, uint16_t length) {
/*   // 1. Wait for previous transfer to finish (skip on very first call when CNDTR=0)
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
*/
    while(length--) {
        while(!(USART3->ISR & USART_ISR_TXE_TXFNF));

        USART3->TDR = *(data++);
    }

    return;
}