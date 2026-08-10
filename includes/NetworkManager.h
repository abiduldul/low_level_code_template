#ifndef __NETWORKMANAGER_H__
#define __NETWORKMANAGER_H__

#include <stdint.h>
#include <time.h>

enum {
    NETWORK_STATE_INIT,
    NETWORK_STATE_BRINGING_UP,
    NETWORK_STATE_UP,
    NETWORK_STATE_RECOVERING,
    NETWORK_STATE_FATAL
};

enum {
    NETWORK_STATE_DOWN = 1,
    NETWORK_LINK_UP = 0,
    NETWORK_LINK_ERROR = 255
};

typedef struct {
    void    (*init)(void* ctx);
    uint8_t (*up)(void* ctx);
    uint8_t (*down)(void* ctx);
    uint8_t (*link_state)(void* ctx);
    uint8_t (*get_timestamp)(void* ctx, time_t* timestamp);
    void* ctx;
} NetworkInterface_t;

uint8_t NetworkManager_Init(NetworkInterface_t* netif);
uint8_t NetworkManager_WaitConnection(uint32_t timeout_ticks);
uint8_t NetworkManager_GetTimestamp(time_t* timestamp);

extern NetworkInterface_t netif_esp32;
#endif

