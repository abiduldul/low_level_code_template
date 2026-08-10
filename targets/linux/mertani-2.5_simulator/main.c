#include "main.h"

#include "process.h"

#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "UartProcess.h"

int main() {
    Init();

    process_init();
    
    while(1) {
        UartProcess_Loop();
        lpuart1_rx_loop();
        //usart3_rx_loop();
    }

    return 0;
}