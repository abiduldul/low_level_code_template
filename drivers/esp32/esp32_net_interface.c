#include "NetworkManager.h"
#include "lwesp/lwesp.h"

#include "log.h"

typedef struct {
    const char* ssid;
    const char* password;
} wifi_t;

static void esp32_network_init(void* ctx);
static uint8_t esp32_network_up(void* ctx);
static uint8_t esp32_network_down(void* ctx);
static uint8_t esp32_network_state(void* ctx);
static uint8_t esp32_network_get_timestamp(void* ctx, time_t* timestamp);

static wifi_t __wifi = {
    .ssid = "Ruang Firmware",
    .password = "mertani661"
};

NetworkInterface_t netif_esp32 = {
    .init = esp32_network_init,
    .up = esp32_network_up,
    .down = esp32_network_down,
    .link_state = esp32_network_state,
    .get_timestamp = esp32_network_get_timestamp,
    .ctx = &__wifi
};

static void set_sntp_server() {
    if(lwesp_sntp_set_config(1, 0, "id.pool.ntp.org", "pool.ntp.org", 
        NULL, NULL, NULL, 1) != lwespOK) {
        LOG_ERROR("Set sntp error");
    }

    return;
}

static void esp32_network_init(void* ctx) {
    tx_thread_sleep(3000);
/*    uint8_t status;

    tx_thread_sleep(8000);

    if((status = lwesp_sntp_set_config(1, 0, "id.pool.ntp.org", "pool.ntp.org", 
        NULL, NULL, NULL, 1)) != lwespOK) {
        LOG_ERROR("Set sntp error, status = %02X", status);
    }

    tx_thread_sleep(5000);*/
    return;
}

static uint8_t esp32_network_up(void* ctx) {
    wifi_t* wifi = (wifi_t*)ctx;

    lwesp_ip_t ip;
    uint8_t is_dhcp;

    if(lwesp_sta_is_joined()) {
        tx_thread_sleep(2000);

        set_sntp_server();

        return NETWORK_LINK_UP;
    }

    LOG_INFO("Connecting to %s", wifi->ssid);

    if(lwesp_sta_join(wifi->ssid, wifi->password, NULL, NULL, NULL, 1) != lwespOK) {
        LOG_ERROR("Connection failed");

        return NETWORK_LINK_ERROR;
    }

    LOG_INFO("Connected to %s", wifi->ssid);
    lwesp_sta_copy_ip(&ip, NULL, NULL, &is_dhcp);

    LOG_INFO("IP Address: %u.%u.%u.%u, is %s", ip.addr.ip4.addr[0], ip.addr.ip4.addr[1], 
            ip.addr.ip4.addr[2], ip.addr.ip4.addr[3], is_dhcp == 1 ? "dhcp" : "static");

    tx_thread_sleep(2000);
    set_sntp_server();

    return NETWORK_LINK_UP;
}

static uint8_t esp32_network_down(void* ctx) {
    lwesp_sta_quit(NULL, NULL, 1);

    return NETWORK_LINK_ERROR;
}

static uint8_t esp32_network_state(void* ctx) {
    return lwesp_sta_is_joined() == 1 ? NETWORK_LINK_UP : NETWORK_LINK_ERROR;
}

static uint8_t esp32_network_get_timestamp(void* ctx, time_t* timestamp) {
    uint8_t status;
    struct tm dt;

    status = lwesp_sntp_gettime(&dt, NULL, NULL, 1);

    if(status != lwespOK) return -1;

    *timestamp = mktime(&dt);

    return 1;
}