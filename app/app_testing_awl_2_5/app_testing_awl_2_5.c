#include "App.h"
#include "Clock.h"
#include "ESP32.h"
#include "NetworkManager.h"
#include "Logger.h"     
#include "log.h"       
#include "watchdog.h"  

#include <stdint.h>
#include <inttypes.h>
#include <time.h>

#define TZ_OFFSET_SECONDS   (7 * 3600)      /* WIB = UTC+7 */
#define TIMESTAMP_FLOOR     1782417727UL

static void app_testing_awl_2_5_init(void);
static void app_testing_awl_2_5_function(const App_t* self);

static App_t app_testing_awl_2_5 = {
    .app_init     = app_testing_awl_2_5_init,
    .app_function = app_testing_awl_2_5_function,
    .app_name     = "Test",
    .app_version  = "2.0.0"
};

REGISTER_APP(app_testing_awl_2_5);

static void app_testing_awl_2_5_init(void) {
    return;
}

static void app_testing_awl_2_5_function(const App_t* self) {
    uint8_t synced = 0;

    (void)self;
    Logger_Init();
    NetworkManager_Init(&netif_esp32);
    Clock_Init();
    ESP32_Init();

    Watchdog_Kick();

    LOG_INFO("Application started");

    while (1) {
        time_t now = Clock_GetTimestamp();

        if (!synced && (uint32_t)now > TIMESTAMP_FLOOR) {
            LOG_INFO("Clock is valid");
            synced = 1;
        }

        if (synced) {
            struct tm local;
            time_t shifted = now + TZ_OFFSET_SECONDS;

            gmtime_r(&shifted, &local);

            LOG_INFO("Clock: %"PRIu32" (%02d:%02d:%02d WIB)",
                     (uint32_t)now,
                     local.tm_hour, local.tm_min, local.tm_sec);
        } else {
            LOG_INFO("Clock: %"PRIu32" (waiting for sync)", (uint32_t)now);
        }

        Watchdog_Kick(); 
        App_Sleep(1000);
    }
}