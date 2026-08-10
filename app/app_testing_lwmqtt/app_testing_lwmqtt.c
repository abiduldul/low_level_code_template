#include "Logger.h"
#include "log.h"
#include "App.h"
#include "lwesp/lwesp.h"
#include "lwmqtt.h"

#include "ESP32.h"

#include "http.h"

#include <stdio.h>
#include <inttypes.h>

static void app_testing_init();
static void app_testing_function();
static void mqtt_mytopic_cb(const char *topic, uint16_t topic_len, 
    const uint8_t* payload, uint16_t payload_len);

static App_t app_testing_lwmqtt = {.app_init = app_testing_init, 
    .app_function = app_testing_function, .app_name = "App Testing", 
    .app_version = "1.0.0"};

REGISTER_APP(app_testing_lwmqtt);

static void app_testing_init(void) {
    Logger_Init();
    ESP32_Init();

    lwmqtt_init();

    return;
}

static lwmqtt_config_t mqtt_config = {
    .host = "broker.emqx.io",
    .port = 1883,
    .client_id = "mqttx_7da83c8c",
    .username = "",
    .password = "",
    .keepalive_sec = 60,
    .timeout_ms = 5000
};

static void app_testing_function() {
    lwmqtt_err_t status; 
    static uint8_t payload_buffer[256];
    uint16_t payload_buffer_length = 0;
    uint32_t counter;

    while(!lwesp_sta_is_joined()) {
        App_Sleep(2000);
    }

    App_Sleep(8000);

    if((status = lwmqtt_connect(&mqtt_config)) != LWMQTT_OK) {
        LOG_ERROR("Mqtt failed to connect, status = %02X", status);
        while(1);
    } else {
        LOG_INFO("Mqtt is connected");
    }

    if((status = lwmqtt_subscribe("topic/okgas_top", mqtt_mytopic_cb)) != LWMQTT_OK) {
        LOG_ERROR("Subscribe error, status = %02X", status);
        while(1);
    } else {
        LOG_INFO("Topic %s is suscribed", "topic/okgas_top");
    }

    while(1) {
        LOG_INFO("Publishing counter: %"PRIu32"", counter);
        payload_buffer_length = snprintf((char*)payload_buffer, 256, "{\"Counter\":%"PRIu32"}", counter++);

        if((status = lwmqtt_publish("topic/okgas_top", payload_buffer, 
            payload_buffer_length, 0)) != LWMQTT_OK) {
            LOG_ERROR("Publish error, status = %02X", status);
        } else {
            LOG_INFO("Publish success");
        }

        App_Sleep(5000);
    }

    return;
}

static void mqtt_mytopic_cb(const char *topic, uint16_t topic_len, 
    const uint8_t* payload, uint16_t payload_len) {
    LOG_INFO("Topic: %.*s, payload: %.*s", topic_len, topic, payload_len, payload);

    return;
} 
