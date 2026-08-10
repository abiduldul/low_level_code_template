#include "log.h"
#include "App.h"
#include "nvm.h"
#include "Logger.h"
#include "sensor.h"
#include "ModbusClient.h"
#include "NetworkManager.h"
#include "Clock.h"
#include "ESP32.h"
#include "lwesp/lwesp.h"
#include "lwmqtt.h"
#include "watchdog.h"
#include "battery.h"
#include "jsonparser.h"

#include "NetworkManager.h"

#include <stdio.h>
#include <inttypes.h>

#define DEVICE_ID                               "MTI-V4K9811BCM3"
#define DEVICE_TOKEN                            "WNxygKz2X6jwF5D5"

#define DEVICE_CONFIG_VALIDITY                  0xB3D1A892
#define DEVICE_CONFIG_DEFAULT_RECORD_INTERVAL   60
#define DEVICE_CONFIG_DEFAULT_STREAM_INTERVAL   10

#define MQTT_TOPIC_RECORD_FMT                   "v1/mqtt/%s/record"
#define MQTT_TOPIC_RESPONSE_FMT                 "v1/mqtt/%s/response"
#define MQTT_TOPIC_STREAM_FMT                   "v1/mqtt/%s/stream"

#define MQTT_PAYLOAD_FMT                        "{\"data\":[{\"devId\":\"%s\",\"unixTime\":\"%"PRIu32"\",%s}]}"

typedef struct {
    uint32_t validity;
    uint32_t record_interval;
    uint32_t stream_interval;
} config_t;

static config_t usedConfig;

static char mqtt_topic_record[64];
static char mqtt_topic_response[64];
static char mqtt_topic_stream[64];

static void app_ewspens_sender_init();
static void app_ewspens_sender_function();

static void mqtt_response_cb(const char    *topic,
                                    uint16_t       topic_len,
                                    const uint8_t *payload,
                                    uint16_t       payload_len);

static App_t app_ewspens_sender = {.app_init = app_ewspens_sender_init, 
    .app_function = app_ewspens_sender_function, .app_name = "App EWS PENS Sender", 
    .app_version = "1.0.0"};

REGISTER_APP(app_ewspens_sender);
    
static lwmqtt_config_t mqtt_config = {
    .client_id = DEVICE_ID,
    .host = "mqtt.mertani.co.id",
    .port = 1883,
    .keepalive_sec = 60,
    .timeout_ms = 5000,
    .username = DEVICE_ID,
    .password = DEVICE_TOKEN
};

static uint8_t read_config(config_t* config) {
    const uint32_t validity = DEVICE_CONFIG_VALIDITY;
    config_t tmp_config;

    if(NVM_Read(0x0, (uint8_t*)&tmp_config, sizeof(config_t)) != 0) {
        LOG_ERROR("Error reading config");
        LOG_INFO("Using default config");

        config->record_interval = DEVICE_CONFIG_DEFAULT_RECORD_INTERVAL;
        config->stream_interval = DEVICE_CONFIG_DEFAULT_STREAM_INTERVAL;

        return -1;
    }

    if(tmp_config.validity != validity) {
        LOG_WARNING("Config is not valid");
        LOG_INFO("Using default config");

        config->record_interval = DEVICE_CONFIG_DEFAULT_RECORD_INTERVAL;
        config->stream_interval = DEVICE_CONFIG_DEFAULT_STREAM_INTERVAL;
    }

    memcpy(config, &tmp_config, sizeof(config_t));

    return 1;
}

static uint8_t write_config(config_t* config) {
    config_t tmp_config = {.validity = DEVICE_CONFIG_VALIDITY, 
        .record_interval = config->record_interval,
        .stream_interval = config->stream_interval};

    if(NVM_Write(0x0, (const uint8_t*)&tmp_config, sizeof(config_t)) == 0) {
        return 1;
    }

    LOG_ERROR("Error writing config");

    return -1;
}

static void app_ewspens_sender_init(void) {
    snprintf(mqtt_topic_record, sizeof(mqtt_topic_record), MQTT_TOPIC_RECORD_FMT, DEVICE_ID);
    snprintf(mqtt_topic_response, sizeof(mqtt_topic_response), MQTT_TOPIC_RESPONSE_FMT, DEVICE_ID);
    snprintf(mqtt_topic_stream, sizeof(mqtt_topic_stream), MQTT_TOPIC_STREAM_FMT, DEVICE_ID);

    return;
}

static void app_ewspens_sender_function(App_t* app) {
    uint32_t last_record_sec = 0;
    uint32_t last_stream_count = 0;
    char json_buffer[256];
    char sensor_json_buffer[64];
    uint8_t status;
    uint8_t simba_aiw3648_actives[2] = {1, 0};
    const char* simba_aiw3648_ids[2] = {"V4K9-flLev", NULL};
    uint8_t batt_sensor_actives[2] = {1, 0};
    const char* batt_sensor_ids[2] = {"V4K9-batt", NULL};

    Logger_Init();

    read_config(&usedConfig);

    LOG_INFO("Record interval: %"PRIu32"", usedConfig.record_interval);
    LOG_INFO("Stream interval: %"PRIu32"", usedConfig.stream_interval);

    LOG_INFO("Battery percentage: %d", Battery_GetPercentage());

    ModbusClient_Init();
    NetworkManager_Init(&netif_esp32);
    Clock_Init();
    ESP32_Init();

    lwmqtt_init();
    
    Sensor_Init(&simba_aiw3648, simba_aiw3648_actives, simba_aiw3648_ids);
    Sensor_Init(&batt_sensor, batt_sensor_actives, batt_sensor_ids);

    Watchdog_Kick();

    App_Sleep(3000);

    while(NetworkManager_WaitConnection(10000) != NETWORK_LINK_UP) {
        LOG_ERROR("Network error");

        App_Sleep(3000);
    }

    Watchdog_Kick();

    LOG_INFO("Network is connected");

    while(1) {
        if(NetworkManager_WaitConnection(10000) != NETWORK_LINK_UP) {
            LOG_ERROR("Network error");

            App_Sleep(3000);

            continue;
        }

        if(!lwmqtt_is_connected()) {
            LOG_WARNING("MQTT is not connected to the broker");
            LOG_INFO("Trying to connect");

            if((status = lwmqtt_connect(&mqtt_config)) != LWMQTT_OK) {
                LOG_ERROR("MQTT failed to connect to the broker, status = %02X", status);

                App_Sleep(3000);
                continue;
            }

            LOG_INFO("MQTT is connected to the broker");

            if((status = lwmqtt_subscribe(mqtt_topic_response, mqtt_response_cb)) == LWMQTT_OK) {
                LOG_INFO("Topic: %s is subscribed", mqtt_topic_response);
            } else {
                LOG_ERROR("Error subscribing topic: %s, status: %02X", mqtt_topic_response, status);
            }
        }

        uint8_t try = 0;

        while(Sensor_ReadAll() != 1) {
            LOG_ERROR("Error reading sensors");

            if(try > 4) {
                LOG_WARNING("Give up on reading the sensors");

                break;
            }

            App_Sleep(200);
            LOG_INFO("Trying again");
        }

        if(last_stream_count++ >= usedConfig.stream_interval) {
            last_stream_count = 0;

            Sensor_PackedAllJson(sensor_json_buffer);

            snprintf(json_buffer, sizeof(json_buffer), MQTT_PAYLOAD_FMT, DEVICE_ID, 
            (uint32_t)Clock_GetTimestamp(), sensor_json_buffer);

            LOG_INFO("JSON: %s", json_buffer);

            LOG_INFO("Publish to %s", mqtt_topic_stream);

            if((status = lwmqtt_publish(mqtt_topic_stream, (const uint8_t*)json_buffer, strlen(json_buffer), 0)) != LWMQTT_OK) {
                LOG_ERROR("Publish error, status = %02X", status);
            }

            App_Sleep(500);
        }

        if((uint32_t)Clock_GetTimestamp() - last_record_sec >= usedConfig.record_interval) {
            last_record_sec = (((uint32_t)Clock_GetTimestamp()) / usedConfig.record_interval) * usedConfig.record_interval;

            Sensor_PackedAllJson(sensor_json_buffer);

            snprintf(json_buffer, sizeof(json_buffer), MQTT_PAYLOAD_FMT, DEVICE_ID, 
            (uint32_t)last_record_sec, sensor_json_buffer);

            LOG_INFO("JSON: %s", json_buffer);

            LOG_INFO("Publish to %s", mqtt_topic_record);

            if((status = lwmqtt_publish(mqtt_topic_record, (const uint8_t*)json_buffer, strlen(json_buffer), 0)) != LWMQTT_OK) {
                LOG_ERROR("Publish error, status = %02X", status);

                App_Sleep(1000);
                continue;
            }
        }

        Watchdog_Kick();
        
        App_Sleep(1000);
    }

    return;
}

static JsonParser_t jsonParser;

static void mqtt_response_cb(const char    *topic,
                                    uint16_t       topic_len,
                                    const uint8_t *payload,
                                    uint16_t       payload_len) {
    int32_t status_code;
    int32_t interval;

    LOG_INFO("Message incoming, topic: %.*s, payload: %.*s", topic_len, topic, payload_len, payload);

    if(jsonparser_parse(&jsonParser, (const char*)payload, 4) != 1) {
        LOG_ERROR("JSON is not valid");
        jsonparser_free(&jsonParser);

        return;
    }

    if(jsonparser_getinteger(&jsonParser, "status_code", &status_code) != 1) {
        LOG_ERROR("Status code not found");
        jsonparser_free(&jsonParser);

        return;
    }

    if(status_code != 201) {
        LOG_ERROR("Server reply error, status_code: %"PRId32"", status_code);

        return;
    }

    if(jsonparser_getinteger(&jsonParser, "interval", &interval) !=1) {
        LOG_ERROR("Interval not found");
        jsonparser_free(&jsonParser);

        return;
    }

    LOG_INFO("Success, status_code = %"PRId32", interval = %"PRId32"", status_code, interval);

    if(usedConfig.record_interval != (uint32_t)interval) {
        LOG_INFO("Updating the interval");

        usedConfig.record_interval = (uint32_t)interval;

        if(write_config(&usedConfig) != 1) {
            LOG_ERROR("Error on writing the new config");
        } else {
            LOG_INFO("Success on writing the new config");
        }
    } else {
        LOG_INFO("The interval is already the same");
    }
   
    jsonparser_free(&jsonParser);

    return;
}