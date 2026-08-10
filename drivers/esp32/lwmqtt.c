#include "lwmqtt.h"

#include "lwesp/apps/lwesp_mqtt_client.h"
#include "lwesp/lwesp.h"

#include "tx_api.h"

#include <string.h>
#include <stdio.h>

/* ── logging ─────────────────────────────────────────────────────────────── */

#ifndef LWMQTT_LOG_ENABLE
#define LWMQTT_LOG_ENABLE   0
#endif

#ifndef LWMQTT_LOG_LEVEL
#define LWMQTT_LOG_LEVEL    2
#endif

#ifndef LWMQTT_LOG_PRINTF
#define LWMQTT_LOG_PRINTF   printf
#endif

#if LWMQTT_LOG_ENABLE
#  if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wvariadic-macros"
#  endif
#  define MQ_LOGE(fmt, ...)  LWMQTT_LOG_PRINTF("[LWMQTT][E] " fmt "\r\n", ##__VA_ARGS__)
#  define MQ_LOGI(fmt, ...)  do { if (LWMQTT_LOG_LEVEL >= 2) \
                                  LWMQTT_LOG_PRINTF("[LWMQTT][I] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#  define MQ_LOGD(fmt, ...)  do { if (LWMQTT_LOG_LEVEL >= 3) \
                                  LWMQTT_LOG_PRINTF("[LWMQTT][D] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#  if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic pop
#  endif
#else
#  define MQ_LOGE(fmt, ...)  (void)0
#  define MQ_LOGI(fmt, ...)  (void)0
#  define MQ_LOGD(fmt, ...)  (void)0
#endif

/* ── connection state ────────────────────────────────────────────────────── */

typedef enum {
    LWMQTT_STATE_IDLE,          /* lwmqtt_init() not yet called             */
    LWMQTT_STATE_DISCONNECTED,  /* initialized, not connected               */
    LWMQTT_STATE_CONNECTING,    /* waiting for CONNACK                      */
    LWMQTT_STATE_CONNECTED,     /* broker accepted connection               */
    LWMQTT_STATE_RECONNECTING,  /* reconnect task is running                */
} lwmqtt_state_t;

/* ── subscription table entry ────────────────────────────────────────────── */

typedef struct {
    char                topic[LWMQTT_TOPIC_MAX_LEN];
    lwmqtt_message_cb_t cb;
    uint8_t             active;
} lwmqtt_sub_t;

typedef struct {
    char        topic[LWMQTT_TOPIC_MAX_LEN];
    uint16_t    topic_len;
    uint8_t     payload[LWMQTT_MAX_PAYLOAD_LEN];
    uint16_t    payload_len;
} lwmqtt_rx_msg_t;

/* ── module state ────────────────────────────────────────────────────────── */

typedef struct {
    lwesp_mqtt_client_p client;

    lwmqtt_config_t     config;         /* original config values           */
    char                host_buf[LWMQTT_MAX_HOST_LEN];
    char                client_id_buf[LWMQTT_MAX_CRED_LEN];
    char                user_buf[LWMQTT_MAX_CRED_LEN];
    char                pass_buf[LWMQTT_MAX_CRED_LEN];

    lwmqtt_state_t      state;
    uint8_t             user_disconnect; /* set when user calls disconnect() */

    /* Semaphores — all start at 0 */
    TX_SEMAPHORE        connect_sem;    /* signaled on CONNACK              */
    TX_SEMAPHORE        publish_sem;    /* signaled on PUBLISH sent         */
    TX_SEMAPHORE        subscribe_sem;  /* signaled on SUBACK               */
    TX_SEMAPHORE        reconnect_sem;  /* signaled on disconnect event     */
    TX_SEMAPHORE        incoming_msg_sem;        /* signaled on event*/

    /* API Mutex to ensure thread-safety for public functions */
    TX_MUTEX            api_mutex;

    /* Results set by the callback before signaling */
    lwesp_mqtt_conn_status_t connect_status;
    lwespr_t                 publish_result;
    lwespr_t                 subscribe_result;

    /* Subscription table */
    lwmqtt_sub_t        subs[LWMQTT_MAX_SUBSCRIPTIONS];
    TX_MUTEX            subs_mutex;     /* protect table access             */

    lwmqtt_rx_msg_t     rx_msgs[LWMQTT_RX_QUEUE_SIZE];
    TX_QUEUE            free_queue;
    ULONG               free_queue_buf[LWMQTT_RX_QUEUE_SIZE];
    TX_QUEUE            rx_queue;
    ULONG               rx_queue_buf[LWMQTT_RX_QUEUE_SIZE];

    /* Reconnect task */
    TX_THREAD           reconnect_thread;
    ULONG               reconnect_stack[LWMQTT_RECONNECT_STACK_SIZE / sizeof(ULONG)];
    
    TX_THREAD           dispatch_thread;
    ULONG               dispatch_stack[LWMQTT_DISPATCH_STACK_SIZE / sizeof(ULONG)];
} lwmqtt_t;

static lwmqtt_t s;

/* ── forward declarations ────────────────────────────────────────────────── */

static void         mqtt_evt_cb(lwesp_mqtt_client_p client,
                                lwesp_mqtt_evt_t *evt);
static void         reconnect_task_entry(ULONG arg);
static lwmqtt_err_t do_connect(void);
static lwmqtt_err_t do_resubscribe(void);
static ULONG        ms_to_ticks(uint32_t ms);

/* ── message dispatch thread ─────────────────────────────────────────────── */

static void dispatch_task_entry(ULONG arg) {
    (void)arg;

    while (1) {
        ULONG msg_idx;
        /* Wait for an incoming message index from the ISR/callback */
        if (tx_queue_receive(&s.rx_queue, &msg_idx, TX_WAIT_FOREVER) == TX_SUCCESS) {
            
            lwmqtt_rx_msg_t *msg = &s.rx_msgs[msg_idx];

            /* Safely take mutex since we are in a thread context */
            if (tx_mutex_get(&s.subs_mutex, TX_WAIT_FOREVER) == TX_SUCCESS) {
                for (int i = 0; i < LWMQTT_MAX_SUBSCRIPTIONS; i++) {
                    if (!s.subs[i].active) { continue; }

                    uint16_t slen = (uint16_t)strlen(s.subs[i].topic);
                    if (slen == msg->topic_len &&
                        memcmp(s.subs[i].topic, msg->topic, msg->topic_len) == 0) {
                        
                        MQ_LOGD("recv: matched sub[%d] cb=%p", i,
                                (void *)(uintptr_t)s.subs[i].cb);
                        
                        if (s.subs[i].cb != NULL) {
                            /* Trigger user callback (they can use mutexes here!) */
                            s.subs[i].cb(msg->topic, msg->topic_len, 
                                         msg->payload, msg->payload_len);
                        }
                        break;
                    }
                }
                tx_mutex_put(&s.subs_mutex);
            }

            /* Free the buffer by returning its index to the free pool */
            tx_queue_send(&s.free_queue, &msg_idx, TX_NO_WAIT);
        }
    }
}

/* ── reconnect task ──────────────────────────────────────────────────────── */

static void reconnect_task_entry(ULONG arg) {
    (void)arg;

    while (1) {
        /* Wait until a disconnect event signals us */
        tx_semaphore_get(&s.reconnect_sem, TX_WAIT_FOREVER);

        MQ_LOGI("reconnect: triggered%s", "");

        while (!s.user_disconnect) {
            /* Fixed delay before each attempt */
            MQ_LOGI("reconnect: waiting %u ms before retry%s",
                    LWMQTT_RECONNECT_DELAY_MS, "");
            tx_thread_sleep(ms_to_ticks(LWMQTT_RECONNECT_DELAY_MS));

            if (s.user_disconnect) { break; }

            MQ_LOGI("reconnect: attempting connect%s", "");

            lwmqtt_err_t err = do_connect();
            if (err != LWMQTT_OK) {
                MQ_LOGE("reconnect: connect failed err=%d, will retry", err);
                continue;
            }

            /* Restore all active subscriptions */
            err = do_resubscribe();
            if (err != LWMQTT_OK) {
                MQ_LOGE("reconnect: resubscribe failed err=%d, will retry", err);
                /* Disconnect and retry the whole sequence */
                lwesp_mqtt_client_disconnect(s.client);
                s.state = LWMQTT_STATE_RECONNECTING;
                continue;
            }

            MQ_LOGI("reconnect: success%s", "");
            break;
        }
    }
}

/* ── lwesp MQTT event callback ───────────────────────────────────────────── */
static void mqtt_evt_cb(lwesp_mqtt_client_p client, lwesp_mqtt_evt_t *evt) {
    (void)client;

    switch (lwesp_mqtt_client_evt_get_type(client, evt)) {

        case LWESP_MQTT_EVT_CONNECT: {
            s.connect_status = lwesp_mqtt_client_evt_connect_get_status(client, evt);

            if (s.connect_status == LWESP_MQTT_CONN_STATUS_ACCEPTED) {
                s.state = LWMQTT_STATE_CONNECTED;
                MQ_LOGI("evt: CONNACK accepted%s", "");
            } else {
                s.state = LWMQTT_STATE_DISCONNECTED;
                MQ_LOGE("evt: CONNACK rejected status=%d",
                        (int)s.connect_status);
            }
            tx_semaphore_put(&s.connect_sem);
            break;
        }

        case LWESP_MQTT_EVT_DISCONNECT: {
            MQ_LOGI("evt: DISCONNECT user=%d", s.user_disconnect);
            s.state = LWMQTT_STATE_DISCONNECTED;

            if (!s.user_disconnect) {
                /*
                 * Unexpected disconnect — signal the reconnect task.
                 * Put is safe here even if the task hasn't consumed
                 * a previous signal: the semaphore will simply count up
                 * and the task will drain it on its next iteration.
                 */
                s.state = LWMQTT_STATE_RECONNECTING;
                tx_semaphore_put(&s.reconnect_sem);
            }
            break;
        }

        case LWESP_MQTT_EVT_PUBLISH: {
            /*
             * For QoS 0, lwesp may not fire this event at all per docs.
             * We signal regardless to unblock lwmqtt_publish().
             */
            s.publish_result = lwesp_mqtt_client_evt_publish_get_result(client, evt);
            MQ_LOGD("evt: PUBLISH result=%d", (int)s.publish_result);
            tx_semaphore_put(&s.publish_sem);
            break;
        }

        case LWESP_MQTT_EVT_SUBSCRIBE: {
            s.subscribe_result = lwesp_mqtt_client_evt_subscribe_get_result(client, evt);
            MQ_LOGI("evt: SUBACK result=%d", (int)s.subscribe_result);
            tx_semaphore_put(&s.subscribe_sem);
            break;
        }

        case LWESP_MQTT_EVT_UNSUBSCRIBE: {
            MQ_LOGI("evt: UNSUBACK%s", "");
            tx_semaphore_put(&s.subscribe_sem);
            break;
        }

        case LWESP_MQTT_EVT_PUBLISH_RECV: {
            /*
             * Incoming message from broker.
             * Extract topic + payload, match against subscription table,
             * call registered callback. Must not block.
             */
            const char *topic   = lwesp_mqtt_client_evt_publish_recv_get_topic(client, evt);
            uint16_t    tlen    = lwesp_mqtt_client_evt_publish_recv_get_topic_len(client, evt);
            const uint8_t *pay  = lwesp_mqtt_client_evt_publish_recv_get_payload(client, evt);
            uint16_t    plen    = lwesp_mqtt_client_evt_publish_recv_get_payload_len(client, evt);

            MQ_LOGI("evt: PUBLISH_RECV topic=%.*s payload_len=%u",
                    (int)tlen, topic, plen);

            /* Exact-match lookup in subscription table */
            ULONG msg_idx;
            
            /* Fetch an empty buffer index from the free pool queue */
            if (tx_queue_receive(&s.free_queue, &msg_idx, TX_NO_WAIT) == TX_SUCCESS) {
                lwmqtt_rx_msg_t *msg = &s.rx_msgs[msg_idx];
                
                msg->topic_len = (tlen < LWMQTT_TOPIC_MAX_LEN) ? tlen : (LWMQTT_TOPIC_MAX_LEN - 1);
                memcpy(msg->topic, topic, msg->topic_len);
                msg->topic[msg->topic_len] = '\0';
                
                msg->payload_len = (plen <= LWMQTT_MAX_PAYLOAD_LEN) ? plen : LWMQTT_MAX_PAYLOAD_LEN;
                if (msg->payload_len > 0 && pay != NULL) {
                    memcpy(msg->payload, pay, msg->payload_len);
                }
                
                /* Dispatch it to the processing thread */
                tx_queue_send(&s.rx_queue, &msg_idx, TX_NO_WAIT);
            } else {
                MQ_LOGE("recv: dropped message, queue full (topic=%.*s)", (int)tlen, topic);
            }

            break;
        }

        default:
            break;
    }
}

/* ── internal helpers ────────────────────────────────────────────────────── */

static ULONG ms_to_ticks(uint32_t ms) {
    return (ms * TX_TIMER_TICKS_PER_SECOND) / 1000u;
}

/*
 * Build lwesp_mqtt_client_info_t from stored config and call
 * lwesp_mqtt_client_connect(). Blocks on connect_sem until CONNACK.
 */
static lwmqtt_err_t do_connect(void) {
    /* Drain any stale connect signal */
    while (tx_semaphore_get(&s.connect_sem, TX_NO_WAIT) == TX_SUCCESS) {}

    lwesp_mqtt_client_info_t info = {
        .id          = s.config.client_id,
        .user        = s.config.username,
        .pass        = s.config.password,
        .keep_alive  = s.config.keepalive_sec,
    };

    s.state = LWMQTT_STATE_CONNECTING;

    lwespr_t res = lwesp_mqtt_client_connect(
        s.client,
        s.config.host,
        (lwesp_port_t)s.config.port,
        mqtt_evt_cb,
        &info
    );

    if (res != lwespOK) {
        MQ_LOGE("do_connect: lwesp_mqtt_client_connect -> %d", (int)res);
        s.state = LWMQTT_STATE_DISCONNECTED;
        return LWMQTT_ERR_TRANSPORT;
    }

    /* Block until CONNACK fires the callback */
    UINT sem_res = tx_semaphore_get(&s.connect_sem,
                                    ms_to_ticks(s.config.timeout_ms));
    if (sem_res != TX_SUCCESS) {
        MQ_LOGE("do_connect: timeout waiting for CONNACK%s", "");
        s.state = LWMQTT_STATE_DISCONNECTED;
        return LWMQTT_ERR_TIMEOUT;
    }

    if (s.connect_status != LWESP_MQTT_CONN_STATUS_ACCEPTED) {
        MQ_LOGE("do_connect: broker rejected status=%d",
                (int)s.connect_status);
        s.state = LWMQTT_STATE_DISCONNECTED;
        return LWMQTT_ERR_CONNECT;
    }

    return LWMQTT_OK;
}

/*
 * Re-subscribe all active entries in the subscription table.
 * Called after a successful reconnect.
 */
static lwmqtt_err_t do_resubscribe(void) {
    if (tx_mutex_get(&s.subs_mutex, ms_to_ticks(s.config.timeout_ms))
            != TX_SUCCESS) {
        return LWMQTT_ERR_TIMEOUT;
    }

    lwmqtt_err_t result = LWMQTT_OK;

    for (int i = 0; i < LWMQTT_MAX_SUBSCRIPTIONS; i++) {
        if (!s.subs[i].active) { continue; }

        /* Drain stale subscribe signal */
        while (tx_semaphore_get(&s.subscribe_sem, TX_NO_WAIT) == TX_SUCCESS) {}

        MQ_LOGI("resubscribe: topic=%s", s.subs[i].topic);

        lwespr_t res = lwesp_mqtt_client_subscribe(
            s.client,
            s.subs[i].topic,
            LWESP_MQTT_QOS_AT_MOST_ONCE,
            NULL
        );

        if (res != lwespOK) {
            MQ_LOGE("resubscribe: lwesp subscribe failed topic=%s",
                    s.subs[i].topic);
            result = LWMQTT_ERR_TRANSPORT;
            break;
        }

        UINT sem_res = tx_semaphore_get(&s.subscribe_sem,
                                        ms_to_ticks(s.config.timeout_ms));
        if (sem_res != TX_SUCCESS) {
            MQ_LOGE("resubscribe: timeout waiting for SUBACK topic=%s",
                    s.subs[i].topic);
            result = LWMQTT_ERR_TIMEOUT;
            break;
        }

        if (s.subscribe_result != lwespOK) {
            MQ_LOGE("resubscribe: SUBACK failed topic=%s", s.subs[i].topic);
            result = LWMQTT_ERR_TRANSPORT;
            break;
        }

        MQ_LOGI("resubscribe: OK topic=%s", s.subs[i].topic);
    }

    tx_mutex_put(&s.subs_mutex);
    return result;
}

/* ── public API ──────────────────────────────────────────────────────────── */

lwmqtt_err_t lwmqtt_init(void) {
    memset(&s, 0, sizeof(s));
    s.state = LWMQTT_STATE_IDLE;

    /* Allocate lwesp MQTT client structure */
    s.client = lwesp_mqtt_client_new(256, 128);
    if (s.client == NULL) {
        MQ_LOGE("init: lwesp_mqtt_client_new failed%s", "");
        return LWMQTT_ERR_TRANSPORT;
    }

    UINT st = TX_SUCCESS;
    st |= tx_semaphore_create(&s.connect_sem,   (CHAR *)"mq_conn",  0);
    st |= tx_semaphore_create(&s.publish_sem,   (CHAR *)"mq_pub",   0);
    st |= tx_semaphore_create(&s.subscribe_sem, (CHAR *)"mq_sub",   0);
    st |= tx_semaphore_create(&s.reconnect_sem, (CHAR *)"mq_recon", 0);
    st |= tx_mutex_create    (&s.subs_mutex,    (CHAR *)"mq_subs",  TX_NO_INHERIT);
    st |= tx_mutex_create    (&s.api_mutex,     (CHAR *)"mq_api",   TX_NO_INHERIT);

    if (st != TX_SUCCESS) {
        MQ_LOGE("init: tx object creation failed%s", "");
        return LWMQTT_ERR_TRANSPORT;
    }

    st |= tx_queue_create(&s.free_queue, (CHAR *)"mq_free_q", TX_1_ULONG, 
                          s.free_queue_buf, sizeof(s.free_queue_buf));
    st |= tx_queue_create(&s.rx_queue,   (CHAR *)"mq_rx_q",   TX_1_ULONG, 
                          s.rx_queue_buf,   sizeof(s.rx_queue_buf));

    if (st != TX_SUCCESS) {
        MQ_LOGE("init: tx object creation failed%s", "");
        return LWMQTT_ERR_TRANSPORT;
    }

    /* Seed the free queue with buffer indices */
    for (ULONG i = 0; i < LWMQTT_RX_QUEUE_SIZE; i++) {
        tx_queue_send(&s.free_queue, &i, TX_NO_WAIT);
    }

    st |= tx_thread_create(&s.reconnect_thread, (CHAR *)"lwmqtt_reconnect", reconnect_task_entry, 0,
                           s.reconnect_stack, sizeof(s.reconnect_stack),
                           LWMQTT_RECONNECT_PRIORITY, LWMQTT_RECONNECT_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START);
                           
    st |= tx_thread_create(&s.dispatch_thread, (CHAR *)"lwmqtt_dispatch", dispatch_task_entry, 0,
                           s.dispatch_stack, sizeof(s.dispatch_stack),
                           LWMQTT_DISPATCH_PRIORITY, LWMQTT_DISPATCH_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START);

    if (st != TX_SUCCESS) {
        MQ_LOGE("init: tx_thread_create failed status=%u", status);
        return LWMQTT_ERR_TRANSPORT;
    }

    s.state = LWMQTT_STATE_DISCONNECTED;
    MQ_LOGI("init: ready%s", "");
    return LWMQTT_OK;
}

lwmqtt_err_t lwmqtt_connect(const lwmqtt_config_t *config) {
    if (config == NULL)               { return LWMQTT_ERR_INVALID_ARG; }
    if (config->client_id == NULL)    { return LWMQTT_ERR_INVALID_ARG; }
    if (config->host == NULL)         { return LWMQTT_ERR_INVALID_ARG; }
    if (s.state == LWMQTT_STATE_IDLE) { return LWMQTT_ERR_INVALID_ARG; }

    tx_mutex_get(&s.api_mutex, TX_WAIT_FOREVER);

    if (s.state != LWMQTT_STATE_DISCONNECTED) {
        MQ_LOGE("connect: invalid state or already connected%s", "");
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_ALREADY;
    }

    /* Store config — reconnect task uses it */
    s.config         = *config;
    s.user_disconnect = 0;

    /* Deep copy strings to prevent dangling pointers */
    strncpy(s.host_buf, config->host, LWMQTT_MAX_HOST_LEN - 1);
    s.host_buf[LWMQTT_MAX_HOST_LEN - 1] = '\0';
    
    strncpy(s.client_id_buf, config->client_id, LWMQTT_MAX_CRED_LEN - 1);
    s.client_id_buf[LWMQTT_MAX_CRED_LEN - 1] = '\0';

    if (config->username) {
        strncpy(s.user_buf, config->username, LWMQTT_MAX_CRED_LEN - 1);
        s.user_buf[LWMQTT_MAX_CRED_LEN - 1] = '\0';
    } else {
        s.user_buf[0] = '\0';
    }

    if (config->password) {
        strncpy(s.pass_buf, config->password, LWMQTT_MAX_CRED_LEN - 1);
        s.pass_buf[LWMQTT_MAX_CRED_LEN - 1] = '\0';
    } else {
        s.pass_buf[0] = '\0';
    }

    MQ_LOGI("connect: host=%s port=%u client_id=%s",
            config->host, config->port, config->client_id);

    lwmqtt_err_t err = do_connect();
    tx_mutex_put(&s.api_mutex);
    return err;
}

lwmqtt_err_t lwmqtt_disconnect(void) {
    tx_mutex_get(&s.api_mutex, TX_WAIT_FOREVER);

    if (s.state == LWMQTT_STATE_IDLE ||
        s.state == LWMQTT_STATE_DISCONNECTED) {
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_DISCONNECTED;
    }

    MQ_LOGI("disconnect: user requested%s", "");

    /* Set flag before closing — prevents reconnect task from triggering */
    s.user_disconnect = 1;

    lwespr_t res = lwesp_mqtt_client_disconnect(s.client);
    if (res != lwespOK) {
        MQ_LOGE("disconnect: lwesp_mqtt_client_disconnect -> %d", (int)res);
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_TRANSPORT;
    }

    s.state = LWMQTT_STATE_DISCONNECTED;
    MQ_LOGI("disconnect: done%s", "");
    tx_mutex_put(&s.api_mutex);
    return LWMQTT_OK;
}

lwmqtt_err_t lwmqtt_publish(const char    *topic,
                             const uint8_t *payload,
                             uint16_t       len,
                             uint8_t        retain) {
    if (topic == NULL) { return LWMQTT_ERR_INVALID_ARG; }

    tx_mutex_get(&s.api_mutex, TX_WAIT_FOREVER);

    if (s.state != LWMQTT_STATE_CONNECTED) {
        MQ_LOGE("publish: not connected%s", "");
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_DISCONNECTED;
    }

    /* Drain any stale publish signal */
    while (tx_semaphore_get(&s.publish_sem, TX_NO_WAIT) == TX_SUCCESS) {}

    MQ_LOGI("publish: topic=%s len=%u retain=%u", topic, len, retain);

    lwespr_t res = lwesp_mqtt_client_publish(
        s.client,
        topic,
        payload,
        len,
        LWESP_MQTT_QOS_AT_MOST_ONCE,
        retain,
        NULL
    );

    if (res != lwespOK) {
        MQ_LOGE("publish: lwesp_mqtt_client_publish -> %d", (int)res);
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_TRANSPORT;
    }

    /*
     * QoS 0 is fire-and-forget. Do NOT block on publish_sem,
     * as lwesp may not fire the PUBLISH event, avoiding a full timeout delay.
     */

    MQ_LOGI("publish: OK%s", "");
    tx_mutex_put(&s.api_mutex);
    return LWMQTT_OK;
}

lwmqtt_err_t lwmqtt_subscribe(const char *topic, lwmqtt_message_cb_t cb) {
    if (topic == NULL || cb == NULL) { return LWMQTT_ERR_INVALID_ARG; }
    if (strlen(topic) >= LWMQTT_TOPIC_MAX_LEN) { return LWMQTT_ERR_INVALID_ARG; }

    tx_mutex_get(&s.api_mutex, TX_WAIT_FOREVER);

    if (s.state != LWMQTT_STATE_CONNECTED) {
        MQ_LOGE("subscribe: not connected%s", "");
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_DISCONNECTED;
    }

    /* ── 1. Register in subscription table ─────────────────────────────── */
    if (tx_mutex_get(&s.subs_mutex, ms_to_ticks(s.config.timeout_ms))
            != TX_SUCCESS) {
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_TIMEOUT;
    }

    int slot = -1;
    for (int i = 0; i < LWMQTT_MAX_SUBSCRIPTIONS; i++) {
        /* Reuse slot if same topic already registered */
        if (s.subs[i].active &&
            strcmp(s.subs[i].topic, topic) == 0) {
            s.subs[i].cb = cb;
            tx_mutex_put(&s.subs_mutex);
            MQ_LOGI("subscribe: updated cb for existing topic=%s", topic);
            tx_mutex_put(&s.api_mutex);
            return LWMQTT_OK;   /* already subscribed at broker level */
        }
        if (slot < 0 && !s.subs[i].active) { slot = i; }
    }

    if (slot < 0) {
        tx_mutex_put(&s.subs_mutex);
        MQ_LOGE("subscribe: table full (max=%d)", LWMQTT_MAX_SUBSCRIPTIONS);
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_FULL;
    }

    strncpy(s.subs[slot].topic, topic, LWMQTT_TOPIC_MAX_LEN - 1);
    s.subs[slot].topic[LWMQTT_TOPIC_MAX_LEN - 1] = '\0';
    s.subs[slot].cb     = cb;
    s.subs[slot].active = 1;

    tx_mutex_put(&s.subs_mutex);

    /* ── 2. Send SUBSCRIBE to broker ────────────────────────────────────── */

    /* Drain any stale subscribe signal */
    while (tx_semaphore_get(&s.subscribe_sem, TX_NO_WAIT) == TX_SUCCESS) {}

    MQ_LOGI("subscribe: topic=%s slot=%d", topic, slot);

    lwespr_t res = lwesp_mqtt_client_subscribe(
        s.client,
        topic,
        LWESP_MQTT_QOS_AT_MOST_ONCE,
        NULL
    );

    if (res != lwespOK) {
        /* Roll back table entry on transport failure */
        tx_mutex_get(&s.subs_mutex, TX_WAIT_FOREVER);
        s.subs[slot].active = 0;
        tx_mutex_put(&s.subs_mutex);
        MQ_LOGE("subscribe: lwesp subscribe failed -> %d", (int)res);
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_TRANSPORT;
    }

    /* Wait for SUBACK */
    UINT sem_res = tx_semaphore_get(&s.subscribe_sem,
                                    ms_to_ticks(s.config.timeout_ms));
    if (sem_res != TX_SUCCESS) {
        /* Roll back on timeout */
        tx_mutex_get(&s.subs_mutex, TX_WAIT_FOREVER);
        s.subs[slot].active = 0;
        tx_mutex_put(&s.subs_mutex);
        MQ_LOGE("subscribe: timeout waiting for SUBACK topic=%s", topic);
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_TIMEOUT;
    }

    if (s.subscribe_result != lwespOK) {
        tx_mutex_get(&s.subs_mutex, TX_WAIT_FOREVER);
        s.subs[slot].active = 0;
        tx_mutex_put(&s.subs_mutex);
        MQ_LOGE("subscribe: SUBACK rejected topic=%s", topic);
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_TRANSPORT;
    }

    MQ_LOGI("subscribe: OK topic=%s", topic);
    tx_mutex_put(&s.api_mutex);
    return LWMQTT_OK;
}

lwmqtt_err_t lwmqtt_unsubscribe(const char *topic) {
    if (topic == NULL) { return LWMQTT_ERR_INVALID_ARG; }

    tx_mutex_get(&s.api_mutex, TX_WAIT_FOREVER);

    if (s.state != LWMQTT_STATE_CONNECTED) {
        MQ_LOGE("unsubscribe: not connected%s", "");
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_DISCONNECTED;
    }

    /* ── 1. Find and deactivate slot ────────────────────────────────────── */
    if (tx_mutex_get(&s.subs_mutex, ms_to_ticks(s.config.timeout_ms))
            != TX_SUCCESS) {
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_TIMEOUT;
    }

    int slot = -1;
    for (int i = 0; i < LWMQTT_MAX_SUBSCRIPTIONS; i++) {
        if (s.subs[i].active &&
            strcmp(s.subs[i].topic, topic) == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        tx_mutex_put(&s.subs_mutex);
        MQ_LOGE("unsubscribe: topic not found: %s", topic);
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_NOT_FOUND;
    }

    /* Deactivate immediately — no more callbacks for this topic */
    s.subs[slot].active = 0;
    tx_mutex_put(&s.subs_mutex);

    /* ── 2. Send UNSUBSCRIBE to broker ──────────────────────────────────── */

    /* Drain stale signal */
    while (tx_semaphore_get(&s.subscribe_sem, TX_NO_WAIT) == TX_SUCCESS) {}

    MQ_LOGI("unsubscribe: topic=%s", topic);

    lwespr_t res = lwesp_mqtt_client_unsubscribe(s.client, topic, NULL);
    if (res != lwespOK) {
        MQ_LOGE("unsubscribe: lwesp unsubscribe failed -> %d", (int)res);
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_TRANSPORT;
    }

    /* Wait for UNSUBACK */
    UINT sem_res = tx_semaphore_get(&s.subscribe_sem,
                                    ms_to_ticks(s.config.timeout_ms));
    if (sem_res != TX_SUCCESS) {
        MQ_LOGE("unsubscribe: timeout waiting for UNSUBACK%s", "");
        tx_mutex_put(&s.api_mutex);
        return LWMQTT_ERR_TIMEOUT;
    }

    MQ_LOGI("unsubscribe: OK topic=%s", topic);
    tx_mutex_put(&s.api_mutex);
    return LWMQTT_OK;
}

int lwmqtt_is_connected(void) {
    return s.state == LWMQTT_STATE_CONNECTED;
}