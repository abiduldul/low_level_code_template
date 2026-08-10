#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdint.h>

typedef enum {
    TELEMETRY_INTERFACE_NOIF,
    TELEMETRY_INTERFACE_WIFI,
    TELEMETRY_INTERFACE_CELLULAR,
    TELEMETRY_INTERFACE_LORA
} telemetry_interface_t;

typedef enum {
    TELEMETRY_PROTOCOL_RAW,
    TELEMETRY_PROTOCOL_MQTT,
    TELEMETRY_PROTOCOL_HTTP
} telemetry_protocol_t;

typedef struct {
	char deviceID[16];
	char deviceToken[32];
	uint32_t deviceInterval;
	uint32_t deviceBatchSize;
	telemetry_interface_t deviceInterface;
	telemetry_protocol_t deviceProtocol;

	char bluetoothName[16];	// Default = "mti-unknown"
	char bluetoothKey[16]; // Default = 123456

	char wifiSSID[16]; // Default = "Guest"
	char wifiPassword[16]; // Default = "mertani2024"

	char gsmAPN[16];

	uint32_t loraDevAddr;
	char loraDevEUI[8];
	char loraNwkSKey[16];
	char loraAppSKey[16];
	uint8_t loraPower;

	char httpAddress[64];
	uint16_t httpPort;
	uint8_t httpSecurity;

	char mqttAddress[64];
	uint16_t mqttPort;
	uint8_t mqttSecurity;
} Config_t;

#endif