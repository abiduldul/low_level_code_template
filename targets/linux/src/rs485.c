#include "rs485.h"
#include "vport.h"

#include <stdatomic.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>

static void rs845_rx_callback(const uint8_t* data, size_t len, void* user);

static const char* rs485_path = "/tmp/rs485";
static vport_t* rs485_port;
static atomic_int rs485_rx_count;
static vport_rx_cb_t rs485_rx_cb = rs845_rx_callback;

static void (*__rs485_rx_callback)(const uint8_t*, uint16_t) = NULL;

void RS485_SetRx_Callback(void (*cb)(const uint8_t* buffer, uint16_t length)) {
    __rs485_rx_callback = cb;
}

void RS485_Init() {
    rs485_port = vport_open(rs485_path, 9600);

    if(rs485_port == NULL) {
        printf(" [!] Failed to open %s\r\n", rs485_path);

        return;
    }

    printf(" Opened fd=%d device=%s\r\n", vport_fd(rs485_port), vport_device(rs485_port));

    atomic_init(&rs485_rx_count, 0);

    vport_irq_attach(rs485_port, rs485_rx_cb, NULL);

    if(vport_irq_enable(rs485_port) != 0) {
        printf(" [!] IRQ enable failed for %s\r\n", rs485_path);

        vport_close(rs485_port);
        return;
    }

    printf("[main] %-6s  fd=%-3d  dev=%s  IRQ enabled\n", 
        rs485_path, vport_fd(rs485_port), vport_device(rs485_port));

    return;
}

void RS485_Send(const uint8_t* buf, uint16_t length) {
    int written = vport_write(rs485_port, buf, length);

    if(written < 0) {
        printf(" [!] Write failed\r\n");
    }

    return;
}

static void rs845_rx_callback(const uint8_t* data, size_t len, void* user) {
    atomic_fetch_add(&rs485_rx_count, (int)len);

    /*printf("[IRQ RS485] %3zu bytes: ", len);
    for (size_t i = 0; i < len; i++)
        printf("%02X ", data[i]);
    //printf("\n");
    fflush(stdout);*/

    if(__rs485_rx_callback != NULL) __rs485_rx_callback(data, len);

    return;
}