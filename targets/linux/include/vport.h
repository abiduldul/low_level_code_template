/**
 * vport.h – Virtual Serial Port Access Library (ThreadX Port)
 * ============================================================
 */

#ifndef VPORT_H
#define VPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vport vport_t;

#define VPORT_DEFAULT_TIMEOUT_MS  500

vport_t *vport_open(const char *path, int baudrate);
void vport_close(vport_t *port);

int vport_write(vport_t *port, const uint8_t *buf, size_t len);
int vport_read(vport_t *port, uint8_t *buf, size_t max_len);
int vport_read_exact(vport_t *port, uint8_t *buf, size_t len);

void vport_set_timeout_ms(vport_t *port, int ms);
void vport_flush(vport_t *port, int queue);

int vport_fd(const vport_t *port);
const char *vport_device(const vport_t *port);

/* ── IRQ subsystem ─────────────────────────────────────────────────────── */

typedef void (*vport_rx_cb_t)(const uint8_t *data, size_t len, void *user);

void vport_irq_attach(vport_t *port, vport_rx_cb_t cb, void *user);
int vport_irq_enable(vport_t *port);
void vport_irq_disable(vport_t *port);
bool vport_irq_running(const vport_t *port);

#ifdef __cplusplus
}
#endif

#endif /* VPORT_H */