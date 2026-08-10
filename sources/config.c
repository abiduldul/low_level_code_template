#include "includes/config.h"
#include <string.h>
#include <stdio.h>

#include "jsonparser.h"

#define CONFIG_MAGIC_NUMBER     0x223388D0

#define CONFIG_JSON_FORMAT "{\"deviceid\":\"%s\",\"deviceinterval\":%u,\"devicebatchsize\":%u,\
                            \"deviceinterface\":%u,\"deviceprotocol\":%u,\"bluetoothname\":\"%s\",\
                            \"wifissid\":\"%s\",\"wifipassword\":\"%s\",\"gsmapn\":\"%s\",\
                            \"loradevaddr\":%u,\"loradeveui\":%s,\"loranwkskey\":\"%s\",\
                            \"loraappskey\":\"%s\",\"lorapower\":%u,\"httpaddress\":\"%s\",\
                            \"httpport\":%u,\"httpsecurity\":%u,\"mqttaddress\":%s,\
                            \"mqttport\":%u,\"mqttsecurity\":%u}"

typedef struct {
    uint32_t magic_number;
    Config_t config;
} Config_Flash_t;

uint8_t config_init(Config_t* config, void* address) {
    if(*((uint32_t*)address) == CONFIG_MAGIC_NUMBER) {
        return -1;
    }

    Config_Flash_t config_flash;

    memcpy((void*)&config_flash, (void*)address, sizeof(Config_Flash_t));

    *config = config_flash.config;

    return 1;
}

void config_write(Config_t* config, char* json_dst) {
    sprintf(json_dst, CONFIG_JSON_FORMAT, config->deviceID, config->deviceToken, config->deviceInterval,
        config->deviceInterface, config->deviceProtocol, config->bluetoothName, config->bluetoothKey,
        config->wifiSSID, config->wifiPassword, config->gsmAPN, config->loraDevAddr, config->loraDevEUI,
        config->loraNwkSKey, config->loraAppSKey, config->loraPower, config->httpAddress, config->httpPort,
        config->httpSecurity, config->mqttAddress, config->mqttPort, config->mqttSecurity);

    return;
}

uint8_t config_parse(Config_t* config, const char* json_src) {
    JsonParser_t jsonParser;

    if(jsonparser_parse(&jsonParser, json_src, 64) != 1) {
        return -1;
    }

    jsonparser_getstring(&jsonParser, "deviceid", config->deviceID);
    jsonparser_getstring(&jsonParser, "devicetoken", config->deviceToken);
    jsonparser_getinteger(&jsonParser, "deviceinterval", &config->deviceInterval);
    jsonparser_getinteger(&jsonParser, "deviceinterface", &config->deviceInterface);
    jsonparser_getinteger(&jsonParser, "deviceprotocol", &config->deviceProtocol);

    jsonparser_getstring(&jsonParser, "bluetoothname", config->bluetoothName);
    jsonparser_getstring(&jsonParser, "bluetoothkey", config->bluetoothKey);

    jsonparser_getstring(&jsonParser, "wifissid", config->wifiSSID);
    jsonparser_getstring(&jsonParser, "wifipassword", config->wifiPassword);

    jsonparser_getsring(&jsonParser, "gsmapn", config->gsmAPN);

    jsonparser_integer(&jsonParser, "loradevaddr", &config->loraDevAddr);
    jsonparser_getstring(&jsonParser, "loradeveui", config->loraDevEUI);
    jsonparser_getstring(&jsonParser, "loranwkskey", config->loraNwkSKey);
    jsonparser_getstring(&jsonParser, "loraappskey", config->loraAppSKey);
    jsonparser_getinteger(&jsonParser, "lorapower", config->loraPower);
    
    jsonparser_getstring(&jsonParser, "httpaddress", config->httpAddress);
    jsonparser_getstring(&jsonParser, "httpport", config->httpPort);
    jsonparser_getinteger(&jsonParser, "httpsecurity", config->httpSecurity);

    jsonparser_getstring(&jsonParser, "mqttaddress", config->mqttAddress);
    jsonparser_getstring(&jsonParser, "mqttport", config->mqttPort);
    jsonparser_getinteger(&jsonParser, "mqttsecurity", config->mqttSecurity);

    jsonparser_free(&jsonParser);

    return 1;
}