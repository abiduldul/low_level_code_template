#ifndef __CIRCULAR_BUFFER_H__
#define __CIRCULAR_BUFFER_H__

#include <stdint.h>

typedef struct {
    uint8_t* buffer;
    uint16_t head;
    uint16_t tail;
    uint16_t length;
} CircularBuffer_t;

void CircularBuffer_Init(CircularBuffer_t* circularBuffer, uint8_t* buffer, uint16_t length);
void CircularBuffer_Push(CircularBuffer_t* circularBuffer, const uint8_t* buffer, uint16_t length);
uint16_t CircularBuffer_Pop(CircularBuffer_t* circularBuffer, uint8_t* buffer);

#endif