#include "CircularBuffer.h"

#include <string.h>
#include <stdio.h>

CircularBuffer_t circularBuffer;
uint8_t buffer[32];

int main() {
    CircularBuffer_Init(&circularBuffer, buffer, 32);

    uint8_t my_buffer[32];

    CircularBuffer_Push(&circularBuffer, (const uint8_t*)"Halo", strlen("Halo"));
    
    CircularBuffer_Pop(&circularBuffer, my_buffer);
    printf("%s\r\n", my_buffer);

    CircularBuffer_Push(&circularBuffer, (const uint8_t*)" Even Flow", strlen(" Even Flow"));

    CircularBuffer_Pop(&circularBuffer, my_buffer);
    printf("%s\r\n", my_buffer);

    return 0;
}