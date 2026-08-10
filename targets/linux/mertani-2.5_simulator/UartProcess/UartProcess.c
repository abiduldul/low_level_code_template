#include "UartProcess.h"

#include <string.h>

extern void lpuart1_transmit(const uint8_t *data, uint16_t length);

static uint8_t nbOfUartProcess;
static UartProcess_t* uartProcesses[8];

static void uartprocess_receiveprocess(UartProcess_t* uartProcess);
static uint16_t software_crc(uint8_t* buf, uint16_t len);

void UartProcess_Init(UartProcess_t* uartProcess, uint8_t id, USART_TypeDef* usart, 
    DMA_Channel_TypeDef* dma_channel, void (*usart_transmit)(const uint8_t*, uint16_t)) {
    uartProcess->usart = usart;
    uartProcess->dma_channel = dma_channel;
    uartProcess->id = id;
    uartProcess->rx_read_pos = 0;
    uartProcess->usart_transmit = usart_transmit;
    
    CircularBuffer_Init(&uartProcess->circularBuffer, uartProcess->uart_circularbuffer, UART_BUFFER_SIZE);

    uartProcesses[nbOfUartProcess++] = uartProcess;

    return;
}

void UartProcess_Callback(UartProcess_t* uartProcess) {
    if(uartProcess->usart->ISR & USART_ISR_IDLE) {
        uartProcess->usart->ICR |= USART_ICR_IDLECF;

        uint16_t curr_write_pos = UART_BUFFER_SIZE - uartProcess->dma_channel->CNDTR;

        if(curr_write_pos >= UART_BUFFER_SIZE) {
            curr_write_pos = 0;
        }

        if(curr_write_pos >= uartProcess->rx_read_pos) {
            uint16_t bytes_received = curr_write_pos - uartProcess->rx_read_pos;

            if(bytes_received > 0) {
                CircularBuffer_Push(&uartProcess->circularBuffer, uartProcess->uart_rx_buffer + 
                    uartProcess->rx_read_pos, bytes_received);
            }
        } else {
            uint16_t bytes_end = UART_BUFFER_SIZE - uartProcess->rx_read_pos;

            uint16_t bytes_head = curr_write_pos;

            CircularBuffer_Push(&uartProcess->circularBuffer, uartProcess->uart_rx_buffer + 
                uartProcess->rx_read_pos, bytes_end);

            if(bytes_head > 0) {
                CircularBuffer_Push(&uartProcess->circularBuffer, uartProcess->uart_rx_buffer, bytes_head);
            }
        }

        uartProcess->rx_read_pos = curr_write_pos;
    }

    uint32_t isr_flags = uartProcess->usart->ISR;

    if(isr_flags & (USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE | USART_ISR_PE)) {
        uartProcess->usart->ICR |= (USART_ICR_ORECF | USART_ICR_NECF | USART_ICR_FECF | USART_ICR_PECF);
    }

    return;
}

void Uart_Process_Transmit(uint8_t id, const uint8_t* data, uint16_t length) {
    for(uint8_t i = 0; i < nbOfUartProcess; i++) {
        if(uartProcesses[i]->id == id) {
            uartProcesses[i]->usart_transmit(data, length);
        }
    }
}

void UartProcess_Loop() {
    for(uint8_t i = 0; i < nbOfUartProcess; i++) {
        uartprocess_receiveprocess(uartProcesses[i]);
    }

    return;
}

static void uartprocess_receiveprocess(UartProcess_t* uartProcess) {
    static uint8_t buffer_process[UART_BUFFER_SIZE + 5];

    uint16_t length;
    
    if((length = CircularBuffer_Pop(&uartProcess->circularBuffer, buffer_process + 3)) == 0) {
        return;
    }

    if (DMA2_Channel3->CNDTR > 0) {
        while (!(DMA2->ISR & DMA_ISR_TCIF3));
    }

    buffer_process[0] = uartProcess->id;

    buffer_process[1] = (uint8_t)length;
    buffer_process[2] = (uint8_t)(length >> 8);

    uint16_t crc = software_crc(buffer_process, length + 3);

    buffer_process[length + 3] = (uint8_t)crc;
    buffer_process[length + 4] = (uint8_t)(crc >> 8);

    // 2. Disable DMAT and DMA channel before reconfiguring
    LPUART1->CR3 &= ~USART_CR3_DMAT;
    DMA2_Channel3->CCR &= ~DMA_CCR_EN;
    while (DMA2_Channel3->CCR & DMA_CCR_EN);

    // 3. Clear all DMA Channel 3 flags
    DMA2->IFCR = DMA_IFCR_CGIF3;

    // 4. Clear UART TX/error flags
    LPUART1->ICR = USART_ICR_TCCF | USART_ICR_ORECF | USART_ICR_FECF;

    // 5. Load buffer and length
    DMA2_Channel3->CMAR  = (uint32_t)buffer_process;
    DMA2_Channel3->CNDTR = length + 5;

    // 6. Enable DMA then DMAT
    DMA2_Channel3->CCR |= DMA_CCR_EN;
    LPUART1->CR3       |= USART_CR3_DMAT;

    return;
}

static uint16_t software_crc(uint8_t* buf, uint16_t len) {
	static const uint16_t table[256] = {
	0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
	0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
	0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
	0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
	0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
	0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
	0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
	0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
	0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
	0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
	0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
	0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
	0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
	0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
	0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
	0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
	0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
	0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
	0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
	0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
	0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
	0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
	0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
	0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
	0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
	0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
	0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
	0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
	0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
	0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
	0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
	0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040 };

	uint8_t xor = 0;
	uint16_t crc = 0xFFFF;

	while( len-- )
	{
		xor = (*buf++) ^ crc;
		crc >>= 8;
		crc ^= table[xor];
	}

	return crc;
}