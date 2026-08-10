#ifndef __HTTP_TRANSPORT_H__
#define __HTTP_TRANSPORT_H__

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int  (*connect)   (void *ctx, const char *host, uint16_t port);
    int  (*send)      (void *ctx, const uint8_t *buf, size_t len);
    int  (*recv)      (void *ctx, uint8_t *buf, size_t len, uint32_t timeout_ms);
    void (*disconnect)(void *ctx);
    void *ctx;
} http_transport_t;

#endif /* __HTTP_TRANSPORT_H__ */