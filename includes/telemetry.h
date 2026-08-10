#ifndef __TELEMETRY_H__
#define __TELEMETRY_H__
#include <stdint.h>

typedef enum {
    TELEMETRY_PROTOCOL_SUCCESS = 0,
    TELEMETRY_PROTOCOL_FAILED,
    TELEMETRY_PROTOCOL_UNSUPPORTED
} telemetry_protocol_status_t;

typedef struct {
    // HTTP Protocol
    telemetry_protocol_status_t (*http_post)(const char* url, const char* content_type, 
        const uint8_t* payload, uint16_t length);
    telemetry_protocol_status_t (*http_get)(const char* url, const char* content_type, 
        const uint8_t* headers, uint16_t length);

    // MQTT Protocol
    telemetry_protocol_status_t (*mqtt_connect)(const char* broker, uint16_t port, 
        const char* client_id);
    telemetry_protocol_status_t (*mqtt_publish)(const char* topic, const uint8_t* payload, 
        uint16_t len, int qos);
    telemetry_protocol_status_t (*mqtt_subscribe)(const char* topic, 
        void (*callback)(const uint8_t*, uint16_t));

    telemetry_protocol_status_t (*raw_send)(uint16_t port, const uint8_t* payload, uint16_t len);

} telemetry_driver_t;

#endif