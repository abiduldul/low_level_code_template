#include "lwesp/lwesp.h"
#include "lwesp/lwesp_input.h"
#include "lwesp/lwesp_mem.h"
#include "system/lwesp_ll.h"

extern void USART3_Config();
extern void USART3_Transmit(const uint8_t *data, uint16_t length);
extern void ESP32_SetRx_Callback(void (*rx_cb)(const uint8_t* buf, uint16_t length));

static uint8_t reset_device(uint8_t state);
static void esp32_rx_cb(const uint8_t* buffer, uint16_t length);
static size_t esp32_send(const void* data, size_t length);

static uint8_t initialized;

lwespr_t lwesp_ll_init(lwesp_ll_t* ll) {
    if (!initialized) {
        ll->send_fn = esp32_send; /* Set callback function to send data */
        ll->reset_fn = reset_device;
    }

    USART3_Config(); /* Initialize UART for communication */
    initialized = 1;

    ESP32_SetRx_Callback(esp32_rx_cb);

    return lwespOK;
}

lwespr_t lwesp_ll_deinit(lwesp_ll_t* ll) {
    initialized = 0; /* Clear initialized flag */

    return lwespOK;
}

static size_t esp32_send(const void* data, size_t length) {
    USART3_Transmit((const uint8_t*)data, length);

    return length;
}

static void esp32_rx_cb(const uint8_t* data, uint16_t length) {
    lwesp_input_process(data, length);

    return;
}

static uint8_t reset_device(uint8_t state) {
    return 0;
}
