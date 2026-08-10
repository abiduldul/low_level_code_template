#include "log.h"
#include "App.h"
#include "Logger.h"
#include "sensor.h"
#include "ModbusClient.h"
#include "NetworkManager.h"
#include "Clock.h"
#include "ESP32.h"
#include "lwesp/lwesp.h"
#include "lwmqtt.h"
#include "watchdog.h"
#include "jsonparser.h"

#include "tx_api.h"
#include "rtos.h"

#include "NetworkManager.h"

#include <stdio.h>
#include <inttypes.h>

#define MQTT_TOPIC_CONTROL_FMT                  "v1/mqtt/%s/control"
#define MQTT_TOPIC_DEVICESTATUS_FMT             "v1/mqtt/%s/deviceStatus"
#define MQTT_TOPIC_STREAM_FMT                   "v1/mqtt/%s/stream"

#define MQTT_PAYLOAD_STATUS_ONLINE              "{\"status\":1}"
#define MQTT_PAYLOAD_STATUS_OFFLINE             "{\"status\":0}"

#define DEVICE_ID                               "MTI-V4K9811BCM3"
#define DEVICE_TOKEN                            "WNxygKz2X6jwF5D5"

#define DEVICE_CONFIG_VALIDITY                  0xB3D1A892
#define DEVICE_CONFIG_DEFAULT_STREAM_INTERVAL   5

#define SPEAKER_ADDRESS                         0x45
#define SPEAKER_REGISTER_PLAY                   0
#define SPEAKER_REGISTER_CONFIG                 1
#define SPEAKER_PLAYTIME_SECONDS                60

typedef struct {
    uint16_t duration;
    uint8_t selection;
    uint8_t play;

    TX_SEMAPHORE sem_speaker;
    TX_THREAD    thread_speaker;
} Speaker_t;

static void app_ewspens_receiver_init();
static void app_ewspens_receiver_function(const App_t*);

static void speaker_entry_function(ULONG);
static void mqtt_topic_control_cb(const char* topic, uint16_t topic_len, 
    const uint8_t* payload, uint16_t payload_len);

static char mqtt_topic_stream[64];
static char mqtt_topic_devicestatus[64];
static char mqtt_topic_control[64];

static Speaker_t speaker;

static App_t app_ewspens_receiver = {.app_init = app_ewspens_receiver_init, 
    .app_function = app_ewspens_receiver_function, .app_name = "App EWS PENS Receiver", 
    .app_version = "1.0.0"};

REGISTER_APP(app_ewspens_receiver);

static lwmqtt_config_t mqtt_config = {
    .client_id = DEVICE_ID,
    .host = "mqtt.mertani.co.id",
    .port = 1883,
    .keepalive_sec = 60,
    .timeout_ms = 5000,
    .username = DEVICE_ID,
    .password = DEVICE_TOKEN
};

static void speaker_init(Speaker_t* speaker) {
    tx_semaphore_create(&speaker->sem_speaker, NULL, 0);

    RTOS_CreateThread(&speaker->thread_speaker, NULL, speaker_entry_function, 1024, 10, 1);

    return;
}

static uint8_t speaker_detect(Speaker_t* speaker, uint32_t timeout) {
    uint8_t try = 0;
    uint8_t max_try = timeout / 1000;
    uint16_t speakerBuffer = 0;

    LOG_INFO("Detecting the speaker amplifier");

    while(ModbusClient_ReadRegisters(SPEAKER_ADDRESS, 0x03, SPEAKER_REGISTER_CONFIG, 1, &speakerBuffer, 1000) != 1) {
        LOG_ERROR("Amplifier is not detected!");

        if(try++ >= max_try) {
            LOG_ERROR("Give up");

            return -1;
        }

        App_Sleep(1000);
        LOG_INFO("Detecting the speaker amplifier");
    }

    LOG_INFO("The amplifier is detected");

    if(speakerBuffer != SPEAKER_PLAYTIME_SECONDS) {
        speakerBuffer = 60;

        ModbusClient_WriteRegisters(SPEAKER_ADDRESS, 0x06, SPEAKER_REGISTER_CONFIG, &speakerBuffer, 1, 1000);
    }

    return 1;
}

static void app_ewspens_receiver_init() {
    snprintf(mqtt_topic_stream, sizeof(mqtt_topic_stream), MQTT_TOPIC_STREAM_FMT, DEVICE_ID);
    snprintf(mqtt_topic_devicestatus, sizeof(mqtt_topic_devicestatus), MQTT_TOPIC_DEVICESTATUS_FMT, DEVICE_ID);
    snprintf(mqtt_topic_control, sizeof(mqtt_topic_control), MQTT_TOPIC_CONTROL_FMT, DEVICE_ID);

    return;
}

static void app_ewspens_receiver_function(const App_t*) {
    uint8_t status;

    Logger_Init();
    ModbusClient_Init();
    NetworkManager_Init(&netif_esp32);
    Clock_Init();
    ESP32_Init();

    lwmqtt_init();

    Watchdog_Kick();

    speaker_init(&speaker);

    if(speaker_detect(&speaker, 10000) != 1) {
        LOG_ERROR("Speaker is not detected");

        while(1) {
            App_Sleep(5000);
        }
    }

    App_Sleep(1000);

    Watchdog_Kick();

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

            if((status = lwmqtt_publish(mqtt_topic_devicestatus, (const uint8_t*)MQTT_PAYLOAD_STATUS_ONLINE, 
                sizeof(MQTT_PAYLOAD_STATUS_ONLINE) - 1, 0)) != LWMQTT_OK) {
                LOG_ERROR("Publish error, status = %02X", status);
            }

            if((status = lwmqtt_subscribe(mqtt_topic_control, mqtt_topic_control_cb)) != LWMQTT_OK) {
                LOG_ERROR("Subscribe error, status = %02X", status);
            }
        }

        App_Sleep(5000);

        Watchdog_Kick();
    }

    return;
}

static JsonParser_t jsonParser;

static void mqtt_topic_control_cb(const char* topic, uint16_t topic_len, 
    const uint8_t* payload, uint16_t payload_len) {
    int32_t ringtoneDuration;
    int32_t ringtone;

    LOG_INFO("Message incoming, topic: %.*s, payload: %.*s", topic_len, topic, payload_len, payload);

    if(jsonparser_parse(&jsonParser, (const char*)payload, 4) != 1) {
        LOG_ERROR("JSON is not valid");
        jsonparser_free(&jsonParser);

        return;
    }

    if(jsonparser_getinteger(&jsonParser, "ringtoneDuration", &ringtoneDuration) == 1) {
        LOG_INFO("Ringtone duration: %"PRId32"", ringtoneDuration);

        speaker.duration = ringtoneDuration;
        tx_semaphore_put(&speaker.sem_speaker);
    } else if(jsonparser_getinteger(&jsonParser, "ringtone", &ringtone) == 1) {
        LOG_INFO("Ringtone selection: %"PRId32"", ringtone);

        speaker.selection = ringtone;
        tx_semaphore_put(&speaker.sem_speaker);
    } else {
        LOG_ERROR("Payload is not valid");
        jsonparser_free(&jsonParser);

        return;
    }

    jsonparser_free(&jsonParser);

    return;
}

static void speaker_on() {
    uint16_t src_register = 0x4443;

    if(ModbusClient_WriteRegisters(SPEAKER_ADDRESS, 0x06, 0x00, &src_register, 1, 1000) != 1) {
        LOG_ERROR("Modbus error, speaker is not turned on");

        return;
    } 

    LOG_INFO("Speaker is turned on");

    return;
}

static void speaker_off() {
    uint16_t src_register = 0x0000;

    if(ModbusClient_WriteRegisters(SPEAKER_ADDRESS, 0x06, 0x00, &src_register, 1, 1000) != 1) {
        LOG_ERROR("Modbus error, speaker is not turned off");

        return;
    } 

    LOG_INFO("Speaker is turned off");

    return;
}

static void speaker_entry_function(ULONG input) {
    while(1) {
        if(tx_semaphore_get(&speaker.sem_speaker, TX_NO_WAIT) == TX_SUCCESS) {
            if(speaker.duration > 0) {
                speaker.play = 1;

                speaker_on();
            } else if(speaker.duration == 0) {
                speaker.play = 0;

                speaker_off();
            }
        }

        if(speaker.play) {
            speaker.duration--;
        }

        if(speaker.duration == 0) {
            speaker_off();
        }

        tx_thread_sleep(1000);
    }
}