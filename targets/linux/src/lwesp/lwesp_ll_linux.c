/**
 * \file            lwesp_ll_posix.c
 * \brief           Low-level communication with ESP device for WIN32
 */

/*
 * Copyright (c) 2024 Tilen MAJERLE
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * This file is part of LwESP - Lightweight ESP-AT parser library.
 *
 * Author:          Tilen MAJERLE <tilen@majerle.eu>
 * Author:          imi415 <imi415.public@gmail.com>
 * Version:         v1.1.2-dev
 */

#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "lwesp/lwesp.h"
#include "lwesp/lwesp_input.h"
#include "lwesp/lwesp_mem.h"
#include "system/lwesp_ll.h"

#include "vport.h"
#include <stdatomic.h>

static uint8_t initialized = 0;

static void uart_thread(void* param);

#define UART_FILE "/tmp/esp32"

static void esp32_rx_callback(const uint8_t* data, size_t length);

static vport_t* esp32_port;
static atomic_int esp32_rx_count;
static vport_rx_cb_t esp32_rx_cb = esp32_rx_callback;

static size_t send_data(const void* data, size_t len) {
    if(len == 0) return -1;

    int written =  vport_write(esp32_port, (const uint8_t*)data, len);
    
    if(written < 0) {
        printf(" [!] Write failed\r\n");
    }

    return written;
}

static void configure_uart(uint32_t baudrate) {
    if (!initialized) {
        esp32_port = vport_open(UART_FILE, baudrate);

        if(esp32_port == NULL) {
            printf(" [!] Failed to open %s\r\n", UART_FILE);

            return;
        }

        printf(" Opened fd=%d device=%s\r\n", vport_fd(esp32_port), vport_device(esp32_port));

        vport_irq_attach(esp32_port, esp32_rx_cb, NULL);

        if(vport_irq_enable(esp32_port) != 0) {
            printf(" [!] IRQ enable failed for %s\r\n", UART_FILE);

            vport_close(esp32_port);
            return;
        }

        printf("[main] %-6s  fd=%-3d  dev=%s  IRQ enabled\n", 
        UART_FILE, vport_fd(esp32_port), vport_device(esp32_port));
    }
}

static uint8_t reset_device(uint8_t state) {
    return 0;
}

lwespr_t lwesp_ll_init(lwesp_ll_t* ll) {
    if (!initialized) {
        ll->send_fn = send_data; /* Set callback function to send data */
        ll->reset_fn = reset_device;
    }

    configure_uart(ll->uart.baudrate); /* Initialize UART for communication */
    initialized = 1;

    return lwespOK;
}

/**
 * \brief           Callback function to de-init low-level communication part
 */
lwespr_t lwesp_ll_deinit(lwesp_ll_t* ll) {
    vport_close(esp32_port);

    initialized = 0; /* Clear initialized flag */
    return lwespOK;
}

static void esp32_rx_callback(const uint8_t* data, size_t length) {
    atomic_fetch_add(&esp32_rx_count, (int)length);
    lwesp_input_process(data, length);

    return;
}
