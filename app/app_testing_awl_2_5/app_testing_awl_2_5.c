#include "App.h"
#include "Clock.h"
#include "ESP32.h"
#include "NetworkManager.h"
#include "Logger.h"
#include "log.h"
#include "watchdog.h"
#include "sensor.h"    
#include "rtc.h"
#include "ModbusClient.h"
#include "pinrainfall.h"

#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>

#define TZ_OFFSET_SECONDS   (7 * 3600)      /* WIB = UTC+7 */
#define TIMESTAMP_FLOOR     1786579200UL    // 13 Agustus 2026

#define JSON_BUFFER_SIZE    512U

/* GPS */
static uint8_t     gps_actives[4] = { 1, 1, 1, 1 };
static const char *gps_ids[4]     = { "GPS-lat", "GPS-lon", "GPS-alt", "GPS-sats" };

/* BGT */
static uint8_t     bgt_actives[5] = { 1, 1, 1, 1, 1 };
static const char *bgt_ids[5]     = { "AirTemp", "AirHum", "Press", "WindSpeed", "WindDir" };

/* Battery */
static uint8_t     batt_actives[2] = { 1, 1 };
static const char *batt_ids[2]     = { "BattPct", "CpuTemp" };

/* Rainfall */
static uint8_t     rain_actives[2] = { 1, 1 };
static const char *rain_ids[2]     = { "RainTips", "RainDur" };

static char json[JSON_BUFFER_SIZE];

static void app_testing_awl_2_5_init();
static void app_testing_awl_2_5_function();

static App_t app_testing_awl_2_5 = {
    .app_init     = app_testing_awl_2_5_init,
    .app_function = app_testing_awl_2_5_function,
    .app_name     = "Test",
    .app_version  = "2.0.0"
};

REGISTER_APP(app_testing_awl_2_5);

static void app_testing_awl_2_5_init(void)
{

    return;
}

static void log_clock(uint8_t *synced)
{
    time_t now = Clock_GetTimestamp();

    if (!*synced && ((uint32_t)now > TIMESTAMP_FLOOR)) {
        LOG_INFO("Clock is valid");
        *synced = 1U;
    }

    if (*synced) {
        struct tm local;
        time_t    shifted = now + TZ_OFFSET_SECONDS;

        gmtime_r(&shifted, &local);

        LOG_INFO("Clock: %"PRIu32" (%02d:%02d:%02d WIB)",
                 (uint32_t)now, local.tm_hour, local.tm_min, local.tm_sec);
    } else {
        LOG_INFO("Clock: %"PRIu32" (waiting for sync)", (uint32_t)now);
    }
}

static void app_testing_awl_2_5_function(App_t* app)
{
    uint8_t synced = 0U;

    Logger_Init();
    NetworkManager_Init(&netif_esp32);
    Clock_Init();
    ESP32_Init();
    PinRainfall_Init(); 
    ModbusClient_Init();

    Sensor_Init(&gps_sensor, gps_actives, gps_ids);
    // Sensor_Init(&bgt_w87x, bgt_actives, bgt_ids);
    Sensor_Init(&batt_sensor, batt_actives, batt_ids);
    Sensor_Init(&tipping_rainfall, rain_actives, rain_ids);
    /* sensorList[8] di sources/sensor.c - maksimal 8 sensor. */

    Watchdog_Kick();

    LOG_INFO("Application started");

    while (1) {
        log_clock(&synced);

        if (Sensor_ReadAll() != 1U) {
            LOG_WARNING("One or more sensors failed this cycle");
        }

        /* Send Data: paketnya dibentuk di sini */
        json[0] = '{';
        Sensor_PackedAllJson(&json[1]);
        strncat(json, "}", JSON_BUFFER_SIZE - strlen(json) - 1U);

        LOG_INFO("%s", json);

        Watchdog_Kick();
        App_Sleep(1000);
    }
}