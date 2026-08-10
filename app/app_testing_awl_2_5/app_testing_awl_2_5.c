#include "App.h"
#include "Logger.h"
#include "log.h"
#include "FileManager.h"
#include "ModbusClient.h"
#include "rtc.h"
#include "sensor.h"

#include <stdio.h>
#include <string.h>

#define LOG_FILENAME        "sensors.jsl"
#define LOG_INTERVAL_MS     20000       /* catat tiap 60 detik */
#define MEDIA_MOUNT_WAIT_MS 3000        /* beri waktu FileManager me-mount media */

#define LINE_MAX            512
#define FRAGMENT_MAX        256

static void app_testing_awl_2_5_init(void);
static void app_testing_awl_2_5_function(const App_t* self);

static App_t app_testing_awl_2_5 = {
    .app_init     = app_testing_awl_2_5_init,
    .app_function = app_testing_awl_2_5_function,
    .app_name     = "Sensor Logger",
    .app_version  = "2.0.0"
};

REGISTER_APP(app_testing_awl_2_5);

static void app_testing_awl_2_5_init(void) {
    Logger_Init();
    FileManager_Init();
    ModbusClient_Init();            /* WAJIB sebelum bgt_w87x dibaca */

    /* Panjang array harus sama dengan nb_values masing-masing sensor. */

    /* batt_sensor: nb_values = 2 */
    uint8_t     batt_actives[2] = {1, 1};
    const char* batt_ids[2]     = {"batt_pct", "cpu_temp"};
    Sensor_Init(&batt_sensor, batt_actives, batt_ids);

    /* bgt_w87x: nb_values = 5 */
    uint8_t     bgt_actives[5] = {1, 1, 1, 1, 1};
    const char* bgt_ids[5]     = {"air_temp", "humidity", "pressure", "windspeed", "winddir"};
    Sensor_Init(&bgt_w87x, bgt_actives, bgt_ids);

    return;
}

static int append_sensor(Sensor_t* sensor, const char* label, char* dst, int capacity) {
    char fragment[FRAGMENT_MAX];

    if(Sensor_Read(sensor) != 1) {
        LOG_ERROR("Baca %s gagal, dilewati", label);
        return 0;
    }

    fragment[0] = 0;

    uint16_t len = Sensor_PackedJson(sensor, fragment);

    if(len == 0) {
        return 0;                   /* tidak ada channel yang aktif */
    }

    fragment[len - 1] = 0;          /* buang koma di ujung */

    int written = snprintf(dst, capacity, ",%s", fragment);

    if(written <= 0 || written >= capacity) {
        LOG_ERROR("Fragmen %s tidak muat di buffer", label);
        return 0;
    }

    return written;
}

static void app_testing_awl_2_5_function(const App_t* self) {
    (void)self;

    char line[LINE_MAX];

    App_Sleep(MEDIA_MOUNT_WAIT_MS);

    LOG_INFO("Sensor logger mulai, target file: %s", LOG_FILENAME);

    while(1) {
        char* cursor    = line;
        int   remaining = sizeof(line);
        int   n;

        /* 1. Buka objek JSON dengan timestamp. */
        n = snprintf(cursor, remaining, "{\"ts\":%lu",
                     (unsigned long)RTC_GetTimestamp());

        cursor    += n;
        remaining -= n;

        /* 2. Tambahkan tiap sensor secara terpisah.
         *    Kalau BGT tidak terhubung, baris tetap berisi data baterai. */
        n = append_sensor(&batt_sensor, "baterai", cursor, remaining);
        cursor    += n;
        remaining -= n;

        n = append_sensor(&bgt_w87x, "BGT W87x", cursor, remaining);
        cursor    += n;
        remaining -= n;

        /* 3. Tutup objek. */
        n = snprintf(cursor, remaining, "}\r\n");

        if(n <= 0 || n >= remaining) {
            LOG_ERROR("Baris log kehabisan ruang, siklus dilewati");
            App_Sleep(LOG_INTERVAL_MS);
            continue;
        }

        cursor += n;

        uint32_t length = (uint32_t)(cursor - line);

        uint8_t status = FileManager_AppendFile(FILEMANAGER_MEDIA_SDCARD,
                                                LOG_FILENAME,
                                                (const uint8_t*)line, &length);

        if(status != 1) {
            LOG_ERROR("Append gagal, status = 0x%02X", status);
        } else {
            LOG_INFO("Tercatat (%lu byte): %.*s",
                     (unsigned long)length, (int)length - 2, line);
        }

        App_Sleep(LOG_INTERVAL_MS);
    }

    return;
}