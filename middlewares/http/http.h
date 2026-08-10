#ifndef __HTTP_H__
#define __HTTP_H__

#include <stdint.h>
#include <stddef.h>

#include "http_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── compile-time configuration ─────────────────────────────────────────── */

#ifndef HTTP_MAX_HEADERS
#define HTTP_MAX_HEADERS        16      /* max request headers              */
#endif

#ifndef HTTP_REQ_BUF_SIZE
#define HTTP_REQ_BUF_SIZE       1024    /* serialized request scratch buf   */
#endif

#ifndef HTTP_RESP_HDR_BUF_SIZE
#define HTTP_RESP_HDR_BUF_SIZE  512     /* raw response header scratch buf  */
#endif

#ifndef HTTP_BODY_BUF_SIZE
#define HTTP_BODY_BUF_SIZE      2048    /* response body buffer             */
#endif

/* ── error codes ────────────────────────────────────────────────────────── */

typedef enum {
    HTTP_OK                 =  0,
    HTTP_ERR_TRANSPORT      = -1,   /* connect / send / recv failed         */
    HTTP_ERR_TIMEOUT        = -2,   /* recv returned no data within timeout */
    HTTP_ERR_PARSE          = -3,   /* malformed status line or headers     */
    HTTP_ERR_BUF_TOO_SMALL  = -4,   /* request or body buffer overflow      */
    HTTP_ERR_INVALID_ARG    = -5,   /* NULL pointer or bad parameter        */
} http_err_t;

/* ── request ────────────────────────────────────────────────────────────── */

typedef struct {
    const char *key;
    const char *value;
} http_header_t;

typedef struct {
    const char    *method;                      /* "GET", "POST", etc.      */
    const char    *host;                        /* "api.example.com"        */
    uint16_t       port;                        /* 80                       */
    const char    *path;                        /* "/v1/data"               */
    http_header_t  headers[HTTP_MAX_HEADERS];   /* extra headers            */
    uint8_t        header_count;
    const uint8_t *body;                        /* NULL for GET/HEAD        */
    size_t         body_len;
} http_request_t;

/* ── response ───────────────────────────────────────────────────────────── */

typedef struct {
    uint16_t  status_code;                      /* 200, 404, 500 …          */
    char      body[HTTP_BODY_BUF_SIZE];
    size_t    body_len;
} http_response_t;

/* ── public API ─────────────────────────────────────────────────────────── */

/**
 * Execute a single HTTP request over the provided transport.
 *
 * Blocking: connect → serialize → send → recv loop → parse → disconnect.
 * Safe to call from any thread; all state is on the stack / in resp.
 *
 * @param transport  Filled http_transport_t (lwesp, POSIX, …)
 * @param req        Request descriptor
 * @param resp       Output; caller-allocated, zeroed on entry by this fn
 * @param timeout_ms Per-recv call timeout; 0 = wait forever
 * @return           HTTP_OK or negative http_err_t
 */
http_err_t http_execute(const http_transport_t *transport,
                        const http_request_t   *req,
                        http_response_t        *resp,
                        uint32_t                timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_H__ */