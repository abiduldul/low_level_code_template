#include "rtos.h"

#include "NetworkManager.h"
#include "inttypes.h"
#include "rtc.h"
#include "log.h"

static TX_THREAD tx_clock;

static void thread_clock_entry(ULONG);

static time_t timestamp_unix;

uint8_t Clock_Init() {
    if(RTOS_CreateThread(&tx_clock, "Clock Thread", thread_clock_entry, 1024, 10, 1) != TX_SUCCESS) return -1;

    return 1;
}

time_t Clock_GetTimestamp() {
    return timestamp_unix;
}

static uint8_t update_time() {
    if(NetworkManager_WaitConnection(100) != NETWORK_LINK_UP) {
        return -1;
    }

    if(NetworkManager_GetTimestamp(&timestamp_unix) == 1) {
        LOG_INFO("Time from the internet: %"PRIu32"", (uint32_t)timestamp_unix);

        if(timestamp_unix <= 1786579200) {
            LOG_ERROR("Time is not valid");
        } else {
            RTC_SetTimestamp(timestamp_unix);

            LOG_INFO("RTC is updated");

            return 1;
        }
    }

    return 0;
}

static void thread_clock_entry(ULONG input) {
    timestamp_unix = RTC_GetTimestamp();
    uint8_t count = 0;
    uint8_t valid = 0;

    while(NetworkManager_WaitConnection(10000) != NETWORK_LINK_UP);

    tx_thread_sleep(2000);

    while(1) {
        if(!valid) {
            if(update_time() == 1) valid = 1;
        }

        if(count++ >= 60) {
            count = 0;
            
            timestamp_unix = RTC_GetTimestamp();
        }

        tx_thread_sleep(1000);

        timestamp_unix++;
    }

    return;
}