#include <stdio.h>
#include <inttypes.h>
#include "system.h"
#include "tx_api.h"

int main() {
    System_Init();
    
    System_DelayMs(1000);
    tx_kernel_enter();

    while(1) {
        
    }

    return 0;
}