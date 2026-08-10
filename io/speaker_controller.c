#include "ModbusClient.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "utils.h"

#define SPEAKER_ADDRESS     0x45
#define SPEAKER_REGISTER_PLAY   0
#define SPEAKER_REGISTER_CONFIG 1

enum {
    ACTION_SPEAKER_WRITEPLAY = 0,
    ACTION_SPEAKER_READPLAY,
    ACTION_SPEAKER_WRITEPLAYTIME,
    ACTION_SPEAKER_READPLAYTIME,
};

typedef struct {
    uint8_t action_type;
    void* ctx;
} Speaker_t;

void speaker_init(void* ctx) {
    return;
}

void speaker_config(void* ctx) {

}

uint8_t speaker_action(void* ctx) {
    Speaker_t* speaker = (Speaker_t*)ctx;
    uint16_t buffer[4] = {0};

    if(speaker->action_type == ACTION_SPEAKER_WRITEPLAY) {
        buffer[0] = 1;

        return ModbusClient_WriteRegisters(SPEAKER_ADDRESS, 0x06, SPEAKER_REGISTER_PLAY, buffer, 1, 1000);
    } else if(speaker->action_type == ACTION_SPEAKER_WRITEPLAYTIME) {
        buffer[0] = *((uint16_t*)speaker->ctx);

        return ModbusClient_WriteRegisters(SPEAKER_ADDRESS, 0x06, SPEAKER_REGISTER_CONFIG, buffer, 
            1, 1000);
    } else if(speaker->action_type == ACTION_SPEAKER_READPLAY) {
        if(ModbusClient_ReadRegisters(SPEAKER_ADDRESS, 0x03, SPEAKER_REGISTER_PLAY, 1, buffer, 1000) != 1) {
            return -2;
        }
        
        *((uint16_t*)speaker->ctx) = buffer[0];

        return 1;
    } else if(speaker->action_type == ACTION_SPEAKER_READPLAYTIME) {
        if(ModbusClient_ReadRegisters(SPEAKER_ADDRESS, 0x03, SPEAKER_REGISTER_CONFIG, 1, buffer, 1000) != 1) {
            return -2;
        }

        *((uint16_t*)speaker->ctx) = buffer[0];

        return 1;
    }

    return -1;
}