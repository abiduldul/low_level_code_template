#include "NetworkManager.h"

#include "rtos.h"

#include "log.h"

#include <stdint.h>

#define NET_FLAG_LINK_UP    (1u << 0)
#define NET_FLAG_LINK_DOWN  (1u << 1)
#define NET_FLAG_FATAL      (1u << 2)

static void networkmanager_thread(ULONG);

static TX_THREAD tx_networkmanager;
static TX_EVENT_FLAGS_GROUP net_flag;
static NetworkInterface_t* p_netif;

uint8_t NetworkManager_Init(NetworkInterface_t* netif) {
    RTOS_CreateThread(&tx_networkmanager, "NetworkManager Thread", networkmanager_thread, 2048, 10, 1);

    tx_event_flags_create(&net_flag, NULL);
    
    p_netif = netif;

    return 1;
}

uint8_t NetworkManager_GetTimestamp(time_t* timestamp) {
    return p_netif->get_timestamp(p_netif->ctx, timestamp);
}

uint8_t NetworkManager_WaitConnection(uint32_t timeout_ticks) {
    ULONG actual_flags = 0;

    UINT status = tx_event_flags_get(
        &net_flag,
        NET_FLAG_LINK_UP | NET_FLAG_FATAL,
        TX_OR,             
        &actual_flags,
        timeout_ticks
    );

    if (status != TX_SUCCESS) {
        return NETWORK_LINK_ERROR; /* timed out */
    }

    if (actual_flags & NET_FLAG_FATAL) {
        return NETWORK_LINK_ERROR;
    }

    return NETWORK_LINK_UP;
}

static void networkmanager_thread(ULONG input) {
    uint8_t state = NETWORK_STATE_INIT;
    uint32_t retry_count = 0;

    p_netif->init(p_netif->ctx);

    while(1) {
        switch(state) {
            case NETWORK_STATE_INIT: {
                LOG_INFO("Initialize network");

                if(p_netif->up(p_netif->ctx) == NETWORK_LINK_UP) {
                    LOG_INFO("Network link is up");
                    state = NETWORK_STATE_BRINGING_UP;
                } else {
                    LOG_INFO("Network recovering");
                    state = NETWORK_STATE_RECOVERING;
                }
                break;
            }
            
            case NETWORK_STATE_BRINGING_UP: {
                uint8_t link_state = p_netif->link_state(p_netif->ctx);

                if(link_state == NETWORK_LINK_UP) {
                    tx_event_flags_set(&net_flag, NET_FLAG_LINK_UP, TX_OR);
                    tx_event_flags_set(&net_flag, ~NET_FLAG_LINK_DOWN, TX_AND);

                    retry_count = 0;
                    state = NETWORK_STATE_UP;
                } else if(link_state == NETWORK_LINK_ERROR) {
                    state = NETWORK_STATE_RECOVERING;
                }
                break;
            }

            case NETWORK_STATE_UP: {
                uint8_t link_state = p_netif->link_state(p_netif->ctx);

                if (link_state != NETWORK_LINK_UP) {
                    tx_event_flags_set(&net_flag,
                                       NET_FLAG_LINK_DOWN, TX_OR);
                    tx_event_flags_set(&net_flag,
                                       ~NET_FLAG_LINK_UP, TX_AND);
                    state = NETWORK_STATE_RECOVERING;
                }
                break;
            }

            case NETWORK_STATE_RECOVERING: {
                //p_netif->down(p_netif->ctx);

                tx_thread_sleep(500);
                if (++retry_count > 5) {
                    state = NETWORK_STATE_FATAL;
                } else {
                    state = NETWORK_STATE_INIT;
                }
                break;
            }

            case NETWORK_STATE_FATAL: {
                LOG_ERROR("Fatal error: Network can't be connected");
                tx_event_flags_set(&net_flag, NET_FLAG_FATAL, TX_OR);

                tx_thread_suspend(tx_thread_identify());
                break;
            }
        }

        tx_thread_sleep(3000);
    }
}