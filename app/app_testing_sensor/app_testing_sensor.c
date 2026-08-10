#include "log.h"
#include "App.h"
#include "Logger.h"
#include "sensor.h"
#include "ModbusClient.h"
#include "ESP32.h"

#include <stdio.h>
#include <string.h>

static void app_testing_init();
static void app_testing_function();

static App_t app_testing_sensor = {.app_init = app_testing_init, 
    .app_function = app_testing_function, .app_name = "App Testing", 
    .app_version = "1.0.0"};

REGISTER_APP(app_testing_sensor);

static void app_testing_init(void) {
    Logger_Init();

    /* Ukuran array HARUS sama dengan nb_values sensornya (batt_sensor = 2) */
    uint8_t     actives[2] = {1, 1};
    const char* ids[2]     = {"batt_pct", "cpu_temp"};

    Sensor_Init(&batt_sensor, actives, ids);
}

static void app_testing_function(const App_t* self) {
    char fragment[512];
    char json[600];

    while (1) {
        if (Sensor_ReadAll() == 1) {
            Sensor_PackedAllJson(fragment);
            snprintf(json, sizeof(json), "{%s}", fragment);
            LOG_INFO("%s", json);
        }
        App_Sleep(5000);
    }
}