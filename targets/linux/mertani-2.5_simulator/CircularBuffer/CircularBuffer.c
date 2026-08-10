#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t* buffer;
    uint16_t head;
    uint16_t tail;
    uint16_t length;
    uint16_t count;
} CircularBuffer_t;

void CircularBuffer_Init(CircularBuffer_t* circularBuffer, uint8_t* buffer, uint16_t length) {
    circularBuffer->buffer = buffer;
    circularBuffer->length = length;
    circularBuffer->head = 0;
    circularBuffer->tail = 0;
    circularBuffer->count = 0;

    return;
}

void CircularBuffer_Push(CircularBuffer_t* circularBuffer, const uint8_t* buffer, uint16_t length) {
    uint16_t space_to_end = circularBuffer->length - circularBuffer->head;

    if(length <= space_to_end) {
        memcpy(circularBuffer->buffer + circularBuffer->head, buffer, length);
        circularBuffer->head = (circularBuffer->head + length) % circularBuffer->length;
    } else {
        uint16_t diff2 = length - space_to_end;

        memcpy(circularBuffer->buffer + circularBuffer->head, buffer, space_to_end);
        memcpy(circularBuffer, buffer + space_to_end, diff2);

        circularBuffer->head = diff2;
    }

    circularBuffer->count += length;

/*    if(circularBuffer->head + length < circularBuffer->length) {
        memcpy(circularBuffer->buffer + circularBuffer->head, buffer, length);

        circularBuffer->head += length;
    } else {
        uint16_t diff = circularBuffer->length - (circularBuffer->head + length);
        uint16_t diff2 = length - diff;

        memcpy(circularBuffer->buffer + circularBuffer->head, buffer, diff);
        memcpy(circularBuffer->buffer, buffer, diff2);

        circularBuffer->head = diff2;
    }*/

    return;
}

uint16_t CircularBuffer_Pop(CircularBuffer_t* circularBuffer, uint8_t* dst) {
    //uint16_t ret = 0;

    if(circularBuffer->count == 0) {
        return 0;
    }

    uint16_t bytes_to_read = circularBuffer->count;

    if(circularBuffer->tail < circularBuffer->head) {
        memcpy(dst, circularBuffer->buffer + circularBuffer->tail, bytes_to_read);
    } else {
        uint16_t space_to_end = circularBuffer->length - circularBuffer->tail;

        memcpy(dst, circularBuffer->buffer + circularBuffer->tail, space_to_end);
        memcpy(dst + space_to_end, circularBuffer->buffer, circularBuffer->head);
    }

    circularBuffer->tail = circularBuffer->head;
    circularBuffer->count = 0;

    return bytes_to_read;
    
    /*if(circularBuffer->tail < circularBuffer->head) {
        ret = circularBuffer->head - circularBuffer->tail;

        memcpy(buffer, circularBuffer->buffer + circularBuffer->tail, ret);
    } else {
        uint16_t diff = circularBuffer->length - circularBuffer->tail;

        memcpy(buffer, circularBuffer->buffer + circularBuffer->tail, diff);
        memcpy(buffer + diff, circularBuffer->buffer, circularBuffer->head);

        ret = diff + circularBuffer->head;
    }

    circularBuffer->tail = circularBuffer->head;

    return ret;*/
}