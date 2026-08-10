#include "lwesp/lwesp.h"
#include "lwesp/lwesp_mem.h"

#include "rtos.h"

#include <log.h>

#include "http.h"

static lwespr_t esp_evt_callback(lwesp_evt_t *evt);

static uint8_t lwesp_memory[0x10000];
const lwesp_mem_region_t lwesp_regions[] = {
    { lwesp_memory, sizeof(lwesp_memory) }
};

void ESP32_Init() {
    lwesp_mem_assignmemory(lwesp_regions, 1);

    lwespr_t status;

    if((status = lwesp_init(esp_evt_callback, 1)) != lwespOK) {
        LOG_ERROR("ESP32 Init Error, status = %02X", status);
    }
    
    //RTOS_CreateThread(&tx_esp32_wifi, NULL, thread_esp32_wifi, 1024, 10, 1);
    return;
}

/*static lwespr_t connect_to_wifi(const char* ssid, const char* password) {
    lwespr_t status;
    uint8_t tried;

    if(lwesp_sta_has_ip()) {
        return lwespOK;
    }

    if((status = lwesp_sta_join(ssid, password, NULL, NULL, NULL, 1)) == lwespOK) {
        lwesp_ip_t ip;
        uint8_t is_dhcp;

        LOG_INFO("Connected to %s", ssid);
        lwesp_sta_copy_ip(&ip, NULL, NULL, &is_dhcp);

        LOG_INFO("IP Address: %u.%u.%u.%u, is %s", ip.addr.ip4.addr[0], ip.addr.ip4.addr[1], 
            ip.addr.ip4.addr[2], ip.addr.ip4.addr[3], is_dhcp == 1 ? "dhcp" : "static");

        return lwespOK;
    }

    return lwespERR;
}

static void thread_esp32_wifi(ULONG input) {
    while(1) {
        tx_thread_sleep(5000);
        
        if(!lwesp_sta_is_joined()) {
            connect_to_wifi("Ruang Firmware", "mertani661");
        }
    }
}*/

static lwespr_t esp_evt_callback(lwesp_evt_t *evt) {
    switch (lwesp_evt_get_type(evt))
    {
        case LWESP_EVT_INIT_FINISH:
            LOG_INFO("[LwESP] Library initialised");
            break;
 
        case LWESP_EVT_RESET:
            LOG_INFO("[LwESP] ESP32 reset");
            break;
 
        case LWESP_EVT_WIFI_CONNECTED:
            LOG_INFO("[LwESP] Wi-Fi connected");
            break;
 
        case LWESP_EVT_WIFI_GOT_IP:
        {
            LOG_INFO("[LwESP] Got IP");
            break;
        }
 
        case LWESP_EVT_WIFI_DISCONNECTED:
            LOG_INFO("[LwESP] Wi-Fi disconnected");
            break;
 
        default:
            break;
    }
    return lwespOK;
}