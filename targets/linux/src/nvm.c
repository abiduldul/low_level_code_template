#include "nvm.h"

#include <string.h>
#include <stdio.h>
#include "storage.h"

#define NVM_SIM_START 0x08000000
#define NVM_SIM_SIZE  (1 * 1024 * 1024)
#define NVM_SIM_UNITIALIZED_BYTE 0xFF

const char* nvm_sim_filename = "nvm_rom.bin";

void NVM_Init() {
    FILE* nvm_sim = fopen(nvm_sim_filename, "rb");

    if(nvm_sim == NULL) {
        uint8_t tmp[NVM_SIM_SIZE];

        memset(tmp, NVM_SIM_UNITIALIZED_BYTE, NVM_SIM_SIZE);

        nvm_sim = fopen(nvm_sim_filename, "wb+");
        fwrite(tmp, sizeof(uint8_t), NVM_SIM_SIZE, nvm_sim);
    }

    fclose(nvm_sim);

    return;
}

uint8_t NVM_Write(uint32_t address, const uint8_t* data, uint16_t length) {
    FILE* nvm_sim = fopen(nvm_sim_filename, "rb+");

    uint32_t start = address - NVM_SIM_START;

    if(start >= NVM_SIM_SIZE) return -1;

    fseek(nvm_sim, start, SEEK_SET);
    fwrite(data, sizeof(uint8_t), length, nvm_sim);
    fclose(nvm_sim);

    return 0;
}

uint8_t NVM_Read(uint32_t address, uint8_t* data, uint16_t length) {
    FILE* nvm_sim = fopen(nvm_sim_filename, "rb");

    uint32_t start = address - NVM_SIM_START;

    if(start >= NVM_SIM_SIZE) return -1;

    fseek(nvm_sim, start, SEEK_SET);
    fread(data, sizeof(uint8_t), 1, nvm_sim);
    fclose(nvm_sim);

    return 0;
}
