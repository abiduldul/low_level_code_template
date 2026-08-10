#include <stdint.h>
#include <string.h>

#include "lwesp/lwesp.h"
#include "lwesp/lwesp_conn.h"

#include "tx_api.h"
#include "http.h"

/* ── logging ─────────────────────────────────────────────────────────────── */

#define LWESP_TRANSPORT_LOG_ENABLE  1
#define HTTP_LOG_ENABLE             1
#define HTTP_LOG_LEVEL              3

#ifndef LWESP_TRANSPORT_LOG_PRINTF
#  ifdef HTTP_LOG_PRINTF
#    define LWESP_TRANSPORT_LOG_PRINTF  HTTP_LOG_PRINTF
#  else
#    define LWESP_TRANSPORT_LOG_PRINTF  printf
#    include <stdio.h>
#  endif
#endif

#if LWESP_TRANSPORT_LOG_ENABLE
#  if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wvariadic-macros"
#  endif
#  define TRP_LOGE(fmt, ...)  LWESP_TRANSPORT_LOG_PRINTF("[LWESP_TRP][E] " fmt "\r\n", ##__VA_ARGS__)
#  define TRP_LOGI(fmt, ...)  do { if (HTTP_LOG_LEVEL >= 2) \
                                   LWESP_TRANSPORT_LOG_PRINTF("[LWESP_TRP][I] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#  define TRP_LOGD(fmt, ...)  do { if (HTTP_LOG_LEVEL >= 3) \
                                   LWESP_TRANSPORT_LOG_PRINTF("[LWESP_TRP][D] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#  if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic pop
#  endif
#else
#  define TRP_LOGE(fmt, ...)  (void)0
#  define TRP_LOGI(fmt, ...)  (void)0
#  define TRP_LOGD(fmt, ...)  (void)0
#endif

static const char *lwesp_res_str(lwespr_t res) {
    switch (res) {
        case lwespOK:       return "lwespOK";
        case lwespCLOSED:   return "lwespCLOSED";
        case lwespTIMEOUT:  return "lwespTIMEOUT";
        case lwespERR:      return "lwespERR";
        default:            return "lwespUNKNOWN";
    }
}

/* ── context ──────────────────────────────────────────────────────────────── */

#define PBUF_QUEUE_SIZE 16

typedef enum {
    CONN_STATE_IDLE,
    CONN_STATE_CONNECTING,
    CONN_STATE_CONNECTED,
    CONN_STATE_CLOSED,
} conn_state_t;

typedef struct {
    lwesp_conn_p  conn;         /* raw lwesp connection handle              */

    /* Circular pointer queue to safely hold unread pbufs                   */
    lwesp_pbuf_p  rx_pbufs[PBUF_QUEUE_SIZE];
    uint8_t       rx_head;     /* index to read from                       */
    uint8_t       rx_tail;     /* index to write to                        */
    size_t        rx_off;      /* byte offset into rx_pbufs[rx_head]       */

    conn_state_t  state;

    /* Semaphores */
    TX_SEMAPHORE  conn_ready;
    TX_SEMAPHORE  send_ready;
    TX_SEMAPHORE  data_ready;

    lwespr_t      conn_result;
    lwespr_t      send_result;
    int           remote_closed;
} lwesp_ctx_t;

static lwesp_ctx_t s_ctx;

/* ── pbuf queue logic ────────────────────────────────────────────────────── */

static void queue_push(lwesp_ctx_t *c, lwesp_pbuf_p pbuf) {
    uint8_t next = (c->rx_tail + 1) % PBUF_QUEUE_SIZE;
    if (next != c->rx_head) {
        c->rx_pbufs[c->rx_tail] = pbuf;
        c->rx_tail = next;
    } else {
        TRP_LOGE("pbuf queue overflow, dropping chunk!");
        /* Acknowledge and drop data to prevent lwESP window locking */
        if (c->conn) lwesp_conn_recved(c->conn, pbuf);
        lwesp_pbuf_free(pbuf);
    }
}

static lwesp_pbuf_p queue_peek(lwesp_ctx_t *c) {
    if (c->rx_head == c->rx_tail) return NULL;
    return c->rx_pbufs[c->rx_head];
}

static void queue_pop(lwesp_ctx_t *c) {
    if (c->rx_head != c->rx_tail) {
        c->rx_head = (c->rx_head + 1) % PBUF_QUEUE_SIZE;
        c->rx_off = 0;
    }
}

static void queue_clear(lwesp_ctx_t *c) {
    while (c->rx_head != c->rx_tail) {
        lwesp_pbuf_p pbuf = c->rx_pbufs[c->rx_head];
        if (pbuf) lwesp_pbuf_free(pbuf);
        c->rx_head = (c->rx_head + 1) % PBUF_QUEUE_SIZE;
    }
    c->rx_off = 0;
}

/* ── lwesp connection event callback ─────────────────────────────────────── */

static lwespr_t conn_evt_fn(lwesp_evt_t *evt) {
    lwesp_ctx_t* c = NULL;
    lwesp_evt_type_t evt_type = lwesp_evt_get_type(evt);

    if(evt_type == LWESP_EVT_CONN_ERROR) {
        c = (lwesp_ctx_t*)lwesp_evt_conn_error_get_arg(evt);
    } else {
        lwesp_conn_p conn = lwesp_evt_conn_active_get_conn(evt);

        if(conn != NULL) {
            c = (lwesp_ctx_t*)lwesp_conn_get_arg(conn);
        }
    }

    if(c == NULL) {
        TRP_LOGE("evt: Context is NULL! Dropping event");
        return lwespOK;
    }

    switch (lwesp_evt_get_type(evt)) {
        case LWESP_EVT_CONN_ACTIVE: {
            c->conn_result = lwespOK;
            c->state       = CONN_STATE_CONNECTED;
            TRP_LOGI("evt: CONN_ACTIVE%s", "");
            tx_semaphore_put(&c->conn_ready);
            break;
        }

        case LWESP_EVT_CONN_ERROR: {
            c->conn_result = lwespERR;
            c->state       = CONN_STATE_IDLE;
            TRP_LOGE("evt: CONN_ERROR%s", "");
            tx_semaphore_put(&c->conn_ready);
            break;
        }

        case LWESP_EVT_CONN_SEND: {
            c->send_result = lwesp_evt_conn_send_get_result(evt);
            TRP_LOGI("evt: CONN_SEND result=%s", lwesp_res_str(c->send_result));
            tx_semaphore_put(&c->send_ready);
            break;
        }

        case LWESP_EVT_CONN_RECV: {
            /* Queue the pointer instead of chaining it with pbuf_cat */
            lwesp_pbuf_p new_pbuf = lwesp_evt_conn_recv_get_buff(evt);
            TRP_LOGI("evt: CONN_RECV pbuf_len=%zu", lwesp_pbuf_length(new_pbuf, 1));
            
            queue_push(c, new_pbuf);
            tx_semaphore_put(&c->data_ready);
            break;
        }

        case LWESP_EVT_CONN_CLOSE: {
            lwespr_t res = lwesp_evt_conn_close_get_result(evt);
            TRP_LOGI("evt: CONN_CLOSE result=%s", lwesp_res_str(res));
            c->state         = CONN_STATE_CLOSED;
            c->remote_closed = 1;
            /* Wake recv() so it can process EOF */
            tx_semaphore_put(&c->data_ready);
            break;
        }

        default:
            break;
    }

    return lwespOK;
}

/* ── helpers ──────────────────────────────────────────────────────────────── */

static size_t drain_pbuf(lwesp_ctx_t *c, uint8_t *buf, size_t len) {
    lwesp_pbuf_p pbuf = queue_peek(c);
    if (pbuf == NULL) return 0;

    const uint8_t *src   = (const uint8_t *)lwesp_pbuf_data(pbuf);
    size_t         avail = lwesp_pbuf_length(pbuf, 1) - c->rx_off;
    size_t         n     = (len < avail) ? len : avail;

    memcpy(buf, src + c->rx_off, n);
    c->rx_off += n;

    TRP_LOGD("drain_pbuf: copied %zu / avail=%zu", n, avail);

    if (c->rx_off >= lwesp_pbuf_length(pbuf, 1)) {
        TRP_LOGD("drain_pbuf: pbuf exhausted, freeing%s", "");
        
        /* Acknowledge bytes to lwESP, free chunk, and remove from queue */
        lwesp_conn_recved(c->conn, pbuf);
        lwesp_pbuf_free(pbuf);
        queue_pop(c);
    }
    return n;
}

/* ── vtable implementations ───────────────────────────────────────────────── */

static int lwesp_transport_connect(void *ctx, const char *host, uint16_t port) {
    lwesp_ctx_t *c = (lwesp_ctx_t *)ctx;

    TRP_LOGI("connect: %s:%u", host, port);

    /* Clean slate */
    queue_clear(c);
    c->state         = CONN_STATE_CONNECTING;
    c->remote_closed = 0;
    c->conn_result   = lwespERR;
    c->send_result   = lwespERR;

    while (tx_semaphore_get(&c->conn_ready, TX_NO_WAIT) == TX_SUCCESS) {}
    while (tx_semaphore_get(&c->data_ready, TX_NO_WAIT) == TX_SUCCESS) {}
    while (tx_semaphore_get(&c->send_ready, TX_NO_WAIT) == TX_SUCCESS) {}

    lwespr_t res = lwesp_conn_start(&c->conn, LWESP_CONN_TYPE_TCP, host, port, c, conn_evt_fn, 0);
    

    if (res != lwespOK) {
        TRP_LOGE("connect: lwesp_conn_connect -> %s", lwesp_res_str(res));
        return -1;
    }

    if (tx_semaphore_get(&c->conn_ready, TX_WAIT_FOREVER) != TX_SUCCESS || c->conn_result != lwespOK) {
        TRP_LOGE("connect: failed result=%s", lwesp_res_str(c->conn_result));
        return -1;
    }

    TRP_LOGI("connect: OK%s", "");
    return 0;
}

static int lwesp_transport_send(void *ctx, const uint8_t *buf, size_t len) {
    lwesp_ctx_t *c = (lwesp_ctx_t *)ctx;

    TRP_LOGI("send: %zu bytes", len);

    if (c->conn == NULL || c->state != CONN_STATE_CONNECTED) {
        TRP_LOGE("send: no active connection%s", "");
        return -1;
    }

    lwespr_t res = lwesp_conn_send(c->conn, buf, len, NULL, 0);

    if (res != lwespOK) {
        TRP_LOGE("send: lwesp_conn_send -> %s", lwesp_res_str(res));
        return -1;
    }

    if (tx_semaphore_get(&c->send_ready, TX_WAIT_FOREVER) != TX_SUCCESS || c->send_result != lwespOK) {
        TRP_LOGE("send: failed result=%s", lwesp_res_str(c->send_result));
        return -1;
    }

    TRP_LOGI("send: OK%s", "");
    return (int)len;
}

static int lwesp_transport_recv(void *ctx, uint8_t *buf, size_t len, uint32_t timeout_ms) {
    lwesp_ctx_t *c = (lwesp_ctx_t *)ctx;

    if (c->conn == NULL) return -1;

    ULONG start_ticks = tx_time_get();
    ULONG timeout_ticks = (timeout_ms == 0) ? TX_WAIT_FOREVER : ((timeout_ms * TX_TIMER_TICKS_PER_SECOND) / 1000u);

    /* Loop gracefully handles stranded semaphores (spurious wakeups) */
    while (1) {
        /* Check for data first */
        size_t drained = drain_pbuf(c, buf, len);
        
        /* FIX: Return immediately if ANY data was found (POSIX non-blocking behavior) */
        if (drained > 0) {
            return (int)drained;
        }

        /* If empty and connection closed, return EOF cleanly */
        if (c->remote_closed && queue_peek(c) == NULL) {
            return 0; 
        }

        /* Check elapsed time */
        ULONG wait_ticks = timeout_ticks;
        if (timeout_ticks != TX_WAIT_FOREVER) {
            ULONG elapsed = tx_time_get() - start_ticks;
            if (elapsed >= timeout_ticks) return -2; /* Timeout */
            wait_ticks = timeout_ticks - elapsed;
        }

        /* Block safely */
        UINT sem_res = tx_semaphore_get(&c->data_ready, wait_ticks);
        
        if (sem_res == TX_NO_INSTANCE) return -2; /* Timeout */
        if (sem_res != TX_SUCCESS) return -1;     /* OS Error */
    }
}

static void lwesp_transport_disconnect(void *ctx) {
    lwesp_ctx_t *c = (lwesp_ctx_t *)ctx;

    TRP_LOGI("disconnect%s", "");

    queue_clear(c);

    if (c->conn != NULL) {
        lwespr_t res = lwesp_conn_close(c->conn, 0);
        TRP_LOGI("disconnect: lwesp_conn_close -> %s", lwesp_res_str(res));
        c->conn  = NULL;
        c->state = CONN_STATE_IDLE;
    }
}

/* ── public init ──────────────────────────────────────────────────────────── */

static http_transport_t s_transport;

http_transport_t *esp32_http_transport_init(void) {
    memset(&s_ctx, 0, sizeof(s_ctx));

    tx_semaphore_create(&s_ctx.conn_ready, (CHAR *)"conn_ready", 0);
    tx_semaphore_create(&s_ctx.send_ready, (CHAR *)"send_ready", 0);
    tx_semaphore_create(&s_ctx.data_ready, (CHAR *)"data_ready", 0);

    s_transport.connect    = lwesp_transport_connect;
    s_transport.send       = lwesp_transport_send;
    s_transport.recv       = lwesp_transport_recv;
    s_transport.disconnect = lwesp_transport_disconnect;
    s_transport.ctx        = &s_ctx;

    TRP_LOGI("init: ready%s", "");
    return &s_transport;
}