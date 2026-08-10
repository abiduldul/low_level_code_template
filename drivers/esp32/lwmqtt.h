#ifndef LWMQTT_H
#define LWMQTT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── compile-time configuration ─────────────────────────────────────────── */

#ifndef LWMQTT_MAX_SUBSCRIPTIONS
#define LWMQTT_MAX_SUBSCRIPTIONS    8
#endif

#ifndef LWMQTT_TOPIC_MAX_LEN
#define LWMQTT_TOPIC_MAX_LEN        128
#endif

#ifndef LWMQTT_MAX_PAYLOAD_LEN
#define LWMQTT_MAX_PAYLOAD_LEN      256
#endif

#ifndef LWMQTT_RECONNECT_DELAY_MS
#define LWMQTT_RECONNECT_DELAY_MS   5000u
#endif

#ifndef LWMQTT_RECONNECT_STACK_SIZE
#define LWMQTT_RECONNECT_STACK_SIZE 1024u
#endif

#ifndef LWMQTT_RECONNECT_PRIORITY
#define LWMQTT_RECONNECT_PRIORITY   12
#endif

#ifndef LWMQTT_MAX_HOST_LEN
#define LWMQTT_MAX_HOST_LEN         64
#endif

#ifndef LWMQTT_MAX_CRED_LEN
#define LWMQTT_MAX_CRED_LEN         64
#endif

#ifndef LWMQTT_DISPATCH_STACK_SIZE
#define LWMQTT_DISPATCH_STACK_SIZE  1024u
#endif

#ifndef LWMQTT_RX_QUEUE_SIZE
#define LWMQTT_RX_QUEUE_SIZE        4
#endif

#ifndef LWMQTT_DISPATCH_PRIORITY
#define LWMQTT_DISPATCH_PRIORITY    10
#endif

/* ── error codes ─────────────────────────────────────────────────────────── */

typedef enum {
    LWMQTT_OK               =  0,
    LWMQTT_ERR_CONNECT      = -1,   /* broker rejected CONNECT              */
    LWMQTT_ERR_TRANSPORT    = -2,   /* lwesp TCP-level failure              */
    LWMQTT_ERR_TIMEOUT      = -3,   /* semaphore wait exceeded timeout      */
    LWMQTT_ERR_INVALID_ARG  = -4,   /* NULL pointer or bad parameter        */
    LWMQTT_ERR_FULL         = -5,   /* subscription table full              */
    LWMQTT_ERR_NOT_FOUND    = -6,   /* topic not in subscription table      */
    LWMQTT_ERR_DISCONNECTED = -7,   /* not connected to broker              */
    LWMQTT_ERR_ALREADY      = -8,   /* already connected                    */
} lwmqtt_err_t;

/* ── types ───────────────────────────────────────────────────────────────── */

/**
 * Callback invoked on the lwesp event thread when a message arrives.
 * Must not block. Copy payload if needed beyond the call.
 */
typedef void (*lwmqtt_message_cb_t)(const char    *topic,
                                    uint16_t       topic_len,
                                    const uint8_t *payload,
                                    uint16_t       payload_len);

typedef struct {
    const char *host;
    uint16_t    port;           /* 1883 plain, 8883 TLS                     */
    const char *client_id;
    const char *username;       /* NULL if unused                           */
    const char *password;       /* NULL if unused                           */
    uint16_t    keepalive_sec;  /* 0 disables keepalive (not recommended)   */
    uint32_t    timeout_ms;     /* connect / subscribe / publish timeout    */
} lwmqtt_config_t;

/* ── public API ──────────────────────────────────────────────────────────── */

/**
 * Initialize lwmqtt internals and create the reconnect task.
 * Call once from tx_application_define() or an init thread before any
 * other lwmqtt function. Does not connect yet.
 *
 * @return LWMQTT_OK or LWMQTT_ERR_TRANSPORT on resource allocation failure
 */
lwmqtt_err_t lwmqtt_init(void);

/**
 * Connect to the MQTT broker and block until CONNACK is received or timeout.
 * Stores config for use by the auto-reconnect task.
 *
 * @return LWMQTT_OK, LWMQTT_ERR_CONNECT, LWMQTT_ERR_TIMEOUT
 */
lwmqtt_err_t lwmqtt_connect(const lwmqtt_config_t *config);

/**
 * Gracefully disconnect. Disables auto-reconnect until lwmqtt_connect()
 * is called again.
 */
lwmqtt_err_t lwmqtt_disconnect(void);

/**
 * Publish a message. Blocks until lwesp confirms the packet was sent
 * to the TCP layer (QoS 0 — no broker ACK).
 *
 * @param topic    Null-terminated topic string
 * @param payload  Raw bytes to publish (may be NULL for zero-length)
 * @param len      Payload length in bytes
 * @param retain   1 = ask broker to retain, 0 = normal
 */
lwmqtt_err_t lwmqtt_publish(const char    *topic,
                             const uint8_t *payload,
                             uint16_t       len,
                             uint8_t        retain);

/**
 * Subscribe to a topic with an exact-match callback.
 * Registers the (topic, cb) pair in the subscription table.
 * Re-subscription on reconnect is handled automatically.
 *
 * @param topic  Null-terminated topic string (≤ LWMQTT_TOPIC_MAX_LEN-1)
 * @param cb     Callback invoked on message receipt; must not block
 */
lwmqtt_err_t lwmqtt_subscribe(const char *topic, lwmqtt_message_cb_t cb);

/**
 * Unsubscribe from a topic and remove it from the subscription table.
 */
lwmqtt_err_t lwmqtt_unsubscribe(const char *topic);

/**
 * Returns 1 if currently connected to the broker, 0 otherwise.
 */
int lwmqtt_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* LWMQTT_H */