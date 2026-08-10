#include "ModbusClient.h"
#include "Logger.h"
#include "log.h"
#include "App.h"

#include "ESP32.h"

#include "lwesp/lwesp.h"
#include "lwesp/lwesp_sntp.h"

#include "http.h"

#include <stdio.h>

static void app_testing_init();
static void app_testing_function(void* ctx);

static App_t app_testing_esp32 = {.app_init = app_testing_init, 
    .app_function = app_testing_function, .app_name = "App Testing", 
    .app_version = "1.0.0"};

REGISTER_APP(app_testing_esp32);

static http_request_t http_request = {
    .method = "GET",
    .host = "api.mertani.co.id",
    .path = "/devices-interval",
    .header_count = 2,
    .body = NULL,
    .body_len = 0,
    .port = 80,
    .headers = {{.key = "Content-Type", .value = "application/json"}, 
    {.key = "X-Authorization-Device", .value = "MTI-A7XFOREVER"}}
};

static http_response_t http_response;

static http_transport_t* http_transport;

extern http_transport_t* esp32_http_transport_init();

static void app_testing_init(void) {
    Logger_Init();
    ESP32_Init();

    http_transport = esp32_http_transport_init();

    return;
}

static void app_testing_function(void* ctx) {
    uint32_t counter = 0;
    /*lwesp_ip_t ip;
    lwesp_ip_t gw;
    lwesp_ip_t nm;*/
    struct tm dt;
    uint8_t status;
    char buffer[256];

    LOG_INFO("Test dulu!");
    App_Sleep(2000);

/*    if((status = lwesp_sta_join("Ruang Firmware", "mertani661", NULL, NULL, NULL, 1)) == lwespOK) {
        LOG_INFO("Successfully connected to the wifi network");

        App_Sleep(5000);

        if((status = lwesp_sta_getip(&ip, &gw, &nm, NULL, NULL, 1)) == lwespOK) {
            LOG_INFO("IP: %u.%u.%u.%u", ip.addr.ip4.addr[0], ip.addr.ip4.addr[1], 
                ip.addr.ip4.addr[2], ip.addr.ip4.addr[3]);
            LOG_INFO("Gateway: %u.%u.%u.%u", gw.addr.ip4.addr[0], gw.addr.ip4.addr[1], 
                gw.addr.ip4.addr[2], gw.addr.ip4.addr[3]);
            LOG_INFO("Netmask: %u.%u.%u.%u", nm.addr.ip4.addr[0], nm.addr.ip4.addr[1], 
                nm.addr.ip4.addr[2], nm.addr.ip4.addr[3]);
        } else {
            LOG_ERROR("Failed to get the IP, status = %02Xh", status);
        }
    } else {
        LOG_ERROR("Connection failed, status = %02Xh", status);
    }
        */

    while(!lwesp_sta_is_joined()) {
        App_Sleep(2000);
    }

    App_Sleep(10000);

    if((status = lwesp_sntp_set_config(1, 7, "id.pool.ntp.org", 
        "pool.ntp.org", NULL, NULL, NULL, 1)) == lwespOK) {
        LOG_INFO("Set sntp server success");
    } else {
        LOG_ERROR("Set sntp server failed");
    }

    App_Sleep(2000);

    while(1) {
        LOG_INFO("Counter: %lu", counter++);
        http_err_t http_err;

        if((http_err = http_execute(http_transport, &http_request, &http_response, 5000)) == HTTP_OK) {
            LOG_INFO("HTTP Request success");
            LOG_INFO("Response: %s, Length: %d, status_code: %d", http_response.body, http_response.body_len, 
                http_response.status_code);
        } else {
            LOG_ERROR("HTTP Request error, status = %02X", http_err);
        }

        /*if ((status = lwesp_sntp_gettime(&dt, NULL, NULL, 1)) == lwespOK) {
            strftime(buffer, sizeof(buffer), "%A, %B %d, %Y - %H:%M:%S", &dt);
            LOG_INFO("Date Time: %s", buffer);
        } else {
            LOG_INFO("Waiting for NTP synchronization..., status = %02Xh\r\n", status);
        }*/
        
        App_Sleep(5000);
    }

    return;
}