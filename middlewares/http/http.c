#include "http.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── logging ─────────────────────────────────────────────────────────────── */
/*
 * Define HTTP_LOG_ENABLE=1 in your build system (or lwhhttp_opts.h) to
 * activate logs. All output goes through HTTP_LOG_PRINTF which you can
 * redirect to any UART/RTT sink.
 *
 * Levels:
 *   HTTP_LOG_LVL_ERROR  (1) — errors only
 *   HTTP_LOG_LVL_INFO   (2) — lifecycle events (connect, send, recv, parse)
 *   HTTP_LOG_LVL_DEBUG  (3) — raw bytes, per-chunk detail
 */
#ifndef HTTP_LOG_ENABLE
#define HTTP_LOG_ENABLE     0
#endif

#ifndef HTTP_LOG_LEVEL
#define HTTP_LOG_LEVEL      2           /* INFO by default                  */
#endif

#ifndef HTTP_LOG_PRINTF
#define HTTP_LOG_PRINTF     printf
#endif

#if HTTP_LOG_ENABLE
#  if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wvariadic-macros"
#  endif
#  define HTTP_LOGE(fmt, ...)  HTTP_LOG_PRINTF("[HTTP][E] " fmt "\r\n", ##__VA_ARGS__)
#  define HTTP_LOGI(fmt, ...)  do { if (HTTP_LOG_LEVEL >= 2) \
                                    HTTP_LOG_PRINTF("[HTTP][I] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#  define HTTP_LOGD(fmt, ...)  do { if (HTTP_LOG_LEVEL >= 3) \
                                    HTTP_LOG_PRINTF("[HTTP][D] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#  if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic pop
#  endif
#else
#  define HTTP_LOGE(fmt, ...)  (void)0
#  define HTTP_LOGI(fmt, ...)  (void)0
#  define HTTP_LOGD(fmt, ...)  (void)0
#endif

/* strncasecmp: POSIX, available on GCC/Clang/MSVC; provide fallback if needed */
#if defined(_MSC_VER)
#  define strncasecmp _strnicmp
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#  include <strings.h>   /* POSIX — provides strncasecmp on Linux/macOS       */
#endif

/* ── internal constants ─────────────────────────────────────────────────── */

#define CRLF                "\r\n"
#define HDR_TERMINATOR      "\r\n\r\n"
#define HDR_TERMINATOR_LEN  4

#define RECV_CHUNK_SIZE     256   /* bytes requested per transport->recv call */

/* ── response parser state machine ─────────────────────────────────────── */

typedef enum {
    PARSE_STATUS_LINE,
    PARSE_HEADERS,
    PARSE_BODY,
    PARSE_DONE,
} parse_state_t;

typedef struct {
    parse_state_t state;

    /* Raw header accumulation */
    char   hdr_buf[HTTP_RESP_HDR_BUF_SIZE];
    size_t hdr_len;

    /* Extracted metadata */
    int    content_length;      /* -1 = unknown (rely on connection close) */
    int    chunked;

    /* Body progress */
    size_t body_received;
} parser_t;

/* ── forward declarations ───────────────────────────────────────────────── */

static http_err_t build_request (const http_request_t *req,
                                 char *buf, size_t buf_size, size_t *out_len);
static http_err_t parser_init   (parser_t *p);
static http_err_t parser_feed   (parser_t *p, const uint8_t *data, size_t len,
                                 http_response_t *resp);
static http_err_t parse_status_line (parser_t *p, http_response_t *resp);
static http_err_t parse_headers     (parser_t *p);

/* ════════════════════════════════════════════════════════════════════════════
 * PUBLIC API
 * ════════════════════════════════════════════════════════════════════════════ */

http_err_t http_execute(const http_transport_t *transport,
                        const http_request_t   *req,
                        http_response_t        *resp,
                        uint32_t                timeout_ms)
{
    if (!transport || !req || !resp) { return HTTP_ERR_INVALID_ARG; }
    if (!req->method || !req->host || !req->path) { return HTTP_ERR_INVALID_ARG; }
 
    memset(resp, 0, sizeof(*resp));
 
    http_err_t err;
 
    /* ── 1. Serialize request ─────────────────────────────────────────── */
    char   req_buf[HTTP_REQ_BUF_SIZE];
    size_t req_len = 0;
 
    err = build_request(req, req_buf, sizeof(req_buf), &req_len);
    if (err != HTTP_OK) {
        HTTP_LOGE("build_request failed: %d", err);
        return err;
    }
    HTTP_LOGI(">> %s %s:%u%s  (headers+line: %zu bytes)",
              req->method, req->host, req->port, req->path, req_len);
    HTTP_LOGD("Request headers:\r\n%.*s", (int)req_len, req_buf);
 
    /* ── 2. Connect ───────────────────────────────────────────────────── */
    HTTP_LOGI("Connecting to %s:%u ...", req->host, req->port);
    if (transport->connect(transport->ctx, req->host, req->port) != 0) {
        HTTP_LOGE("Connect failed%s", "");
        return HTTP_ERR_TRANSPORT;
    }
    HTTP_LOGI("Connected%s", "");
 
    /* ── 3. Send ──────────────────────────────────────────────────────── */
    int sent = transport->send(transport->ctx,
                               (const uint8_t *)req_buf, req_len);
    if (sent < 0 || (size_t)sent != req_len) {
        HTTP_LOGE("Send headers failed: sent=%d expected=%zu", sent, req_len);
        transport->disconnect(transport->ctx);
        return HTTP_ERR_TRANSPORT;
    }
    HTTP_LOGI("Sent headers: %d bytes", sent);
 
    /* Send body if present */
    if (req->body != NULL && req->body_len > 0) {
        sent = transport->send(transport->ctx, req->body, req->body_len);
        if (sent < 0 || (size_t)sent != req->body_len) {
            HTTP_LOGE("Send body failed: sent=%d expected=%zu",
                      sent, req->body_len);
            transport->disconnect(transport->ctx);
            return HTTP_ERR_TRANSPORT;
        }
        HTTP_LOGI("Sent body: %d bytes", sent);
    }
 
    /* ── 4. Receive + parse loop ──────────────────────────────────────── */
    parser_t parser;
    parser_init(&parser);
 
    uint8_t chunk[RECV_CHUNK_SIZE];
    err = HTTP_OK;
    int recv_count = 0;
 
    HTTP_LOGI("Waiting for response (timeout_ms=%u) ...", timeout_ms);
 
    while (parser.state != PARSE_DONE) {
        HTTP_LOGD("recv call #%d  state=%d  body_so_far=%zu",
                  recv_count, (int)parser.state, parser.body_received);
 
        int n = transport->recv(transport->ctx, chunk, sizeof(chunk), timeout_ms);
        recv_count++;
 
        HTTP_LOGD("recv #%d returned %d", recv_count, n);
 
        if (n == -2) {
            /* Transport deadline exceeded — no data within timeout_ms */
            HTTP_LOGE("recv #%d timeout (deadline exceeded)  state=%d  body=%zu/%d",
                      recv_count, (int)parser.state,
                      parser.body_received, parser.content_length);
            err = HTTP_ERR_TIMEOUT;
            break;
        }
 
        if (n < 0) {
            HTTP_LOGE("recv #%d error (%d)  state=%d  body=%zu/%d",
                      recv_count, n,
                      (int)parser.state,
                      parser.body_received, parser.content_length);
            err = HTTP_ERR_TRANSPORT;
            break;
        }
 
        if (n == 0) {
            /*
             * Connection closed by remote.
             * If we were receiving a body with unknown Content-Length
             * (Connection: close pattern), this is normal EOF.
             */
            if (parser.state == PARSE_BODY && parser.content_length == -1) {
                HTTP_LOGI("EOF — connection closed, body complete (%zu bytes)",
                          parser.body_received);
                parser.state = PARSE_DONE;
            } else if (parser.state != PARSE_DONE) {
                HTTP_LOGE("Unexpected EOF  state=%d  body=%zu/%d",
                          (int)parser.state,
                          parser.body_received, parser.content_length);
                err = HTTP_ERR_TRANSPORT;
            }
            break;
        }
 
        HTTP_LOGD("recv #%d  got %d bytes", recv_count, n);
 
        err = parser_feed(&parser, chunk, (size_t)n, resp);
        if (err != HTTP_OK) {
            HTTP_LOGE("parser_feed error %d after recv #%d", err, recv_count);
            break;
        }
 
        /* Guard: parser may have reached DONE inside parser_feed */
        if (parser.state == PARSE_DONE) { break; }
 
        /* Body complete via Content-Length */
        if (parser.state == PARSE_BODY &&
            parser.content_length >= 0 &&
            (int)parser.body_received >= parser.content_length) {
            HTTP_LOGI("Body complete via Content-Length (%d bytes)",
                      parser.content_length);
            parser.state = PARSE_DONE;
        }
    }
 
    /* ── 5. Disconnect ────────────────────────────────────────────────── */
    transport->disconnect(transport->ctx);
 
    if (err == HTTP_OK) {
        HTTP_LOGI("<< status=%u  body=%zu bytes  recv_calls=%d",
                  resp->status_code, resp->body_len, recv_count);
    } else {
        HTTP_LOGE("http_execute failed: err=%d  status=%u  body=%zu  recv_calls=%d",
                  err, resp->status_code, resp->body_len, recv_count);
    }
 
    return err;
}

/* ════════════════════════════════════════════════════════════════════════════
 * REQUEST BUILDER
 * ════════════════════════════════════════════════════════════════════════════ */

static http_err_t build_request(const http_request_t *req,
                                char *buf, size_t buf_size, size_t *out_len)
{
    size_t pos = 0;

/* Safe append macros — return early on overflow */
#define APPEND(fmt, ...) \
    do { \
        int _n = snprintf(buf + pos, buf_size - pos, fmt, __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= buf_size - pos) { \
            return HTTP_ERR_BUF_TOO_SMALL; \
        } \
        pos += (size_t)_n; \
    } while (0)

/* Variant for string literals with no format args */
#define APPEND_STR(s) \
    do { \
        size_t _slen = sizeof(s) - 1; \
        if (pos + _slen >= buf_size) { return HTTP_ERR_BUF_TOO_SMALL; } \
        memcpy(buf + pos, s, _slen); \
        pos += _slen; \
    } while (0)

    /* Request line */
    APPEND("%s %s HTTP/1.0" CRLF, req->method, req->path);

    /* Mandatory Host header */
    APPEND("Host: %s" CRLF, req->host);

    /* Connection: close — simplifies body termination detection */
    APPEND_STR("Connection: close" CRLF);

    /* Content-Length if body present */
    if (req->body != NULL && req->body_len > 0) {
        APPEND("Content-Length: %zu" CRLF, req->body_len);
    } else if(req->method[0] == 'P') {
        APPEND("Content-Length: %zu" CRLF, 0);
    }

    /* Caller-supplied headers */
    for (uint8_t i = 0; i < req->header_count; i++) {
        if (req->headers[i].key && req->headers[i].value) {
            APPEND("%s: %s" CRLF, req->headers[i].key, req->headers[i].value);
        }
    }

    /* Blank line — end of headers */
    APPEND_STR(CRLF);

#undef APPEND

    *out_len = pos;
    return HTTP_OK;
}

/* ════════════════════════════════════════════════════════════════════════════
 * RESPONSE PARSER
 * ════════════════════════════════════════════════════════════════════════════ */

static http_err_t parser_init(parser_t *p)
{
    memset(p, 0, sizeof(*p));
    p->state          = PARSE_STATUS_LINE;
    p->content_length = -1;
    return HTTP_OK;
}

/*
 * Feed raw bytes into the parser.
 * May be called many times; state persists across calls.
 */
static http_err_t parser_feed(parser_t *p, const uint8_t *data, size_t len,
                               http_response_t *resp)
{
    http_err_t err;
    size_t     consumed = 0;

    /* ── STATUS LINE + HEADERS ────────────────────────────────────────── */
    if (p->state == PARSE_STATUS_LINE || p->state == PARSE_HEADERS) {
        /*
         * Accumulate raw bytes into hdr_buf until we see \r\n\r\n.
         * Everything after that belongs to the body.
         */
        size_t copy = len;
        if (p->hdr_len + copy > sizeof(p->hdr_buf) - 1) {
            copy = sizeof(p->hdr_buf) - 1 - p->hdr_len;
        }
        memcpy(p->hdr_buf + p->hdr_len, data, copy);
        p->hdr_len += copy;
        p->hdr_buf[p->hdr_len] = '\0';
        consumed += copy;

        /* Locate end-of-headers marker */
        char *hdr_end = strstr(p->hdr_buf, HDR_TERMINATOR);
        if (hdr_end == NULL) {
            /* Haven't received full headers yet */
            if (consumed < len) {
                /*
                 * hdr_buf is full but marker not found — overflow.
                 * This shouldn't happen with a reasonable HDR_BUF_SIZE.
                 */
                return HTTP_ERR_BUF_TOO_SMALL;
            }
            return HTTP_OK;
        }

        /* Null-terminate the header block for string parsing */
        *hdr_end = '\0';
        size_t hdr_block_len = (size_t)(hdr_end - p->hdr_buf);

        /* Parse status line */
        err = parse_status_line(p, resp);
        if (err != HTTP_OK) {
            HTTP_LOGE("parse_status_line failed: %d", err);
            return err;
        }
        HTTP_LOGI("Status line parsed: %u", resp->status_code);

        /* Parse headers (Content-Length etc.) */
        err = parse_headers(p);
        if (err != HTTP_OK) {
            HTTP_LOGE("parse_headers failed: %d", err);
            return err;
        }
        HTTP_LOGI("Headers parsed: Content-Length=%d  chunked=%d",
                  p->content_length, p->chunked);

        p->state = PARSE_BODY;

        /*
         * Any bytes after \r\n\r\n in this chunk are body bytes.
         * hdr_end points into hdr_buf; body starts at hdr_end + 4.
         */
        size_t body_offset_in_hdr = hdr_block_len + HDR_TERMINATOR_LEN;

        /* Body bytes already copied into hdr_buf past the header block */
        size_t body_in_hdr = p->hdr_len - body_offset_in_hdr;

        /* Also bytes from `data` not yet copied (if copy < len) */
        size_t remaining_in_chunk = len - consumed;

        /* Process body bytes from hdr_buf overflow */
        if (body_in_hdr > 0) {
            err = parser_feed(p,
                              (const uint8_t *)(p->hdr_buf + body_offset_in_hdr),
                              body_in_hdr, resp);
            if (err != HTTP_OK) { return err; }
        }

        /* Process remaining bytes from the original chunk */
        if (remaining_in_chunk > 0) {
            err = parser_feed(p, data + consumed, remaining_in_chunk, resp);
            if (err != HTTP_OK) { return err; }
        }

        return HTTP_OK;
    }

    /* ── BODY ─────────────────────────────────────────────────────────── */
    if (p->state == PARSE_BODY) {
        size_t space = (HTTP_BODY_BUF_SIZE -1) - p->body_received;
        size_t copy  = (len < space) ? len : space;

        if(copy > 0) {
            memcpy(resp->body + resp->body_len, data, copy);
            resp->body_len += copy;
            resp->body[resp->body_len] = 0;
        }

        p->body_received += len;

        /* Check Content-Length completion */
        if (p->content_length >= 0 &&
            (int)p->body_received >= p->content_length) {
            p->state = PARSE_DONE;
        }

        return HTTP_OK;
    }

    return HTTP_OK;
}

/* ── Status line: "HTTP/1.1 200 OK\r\n" ─────────────────────────────────── */

static http_err_t parse_status_line(parser_t *p, http_response_t *resp)
{
    /* hdr_buf contains the full header block, null-terminated at \r\n\r\n */
    char *line_end = strstr(p->hdr_buf, CRLF);
    if (line_end == NULL) { return HTTP_ERR_PARSE; }

    /* Temporarily terminate the first line */
    *line_end = '\0';

    /* Expected: "HTTP/1.x NNN <reason>" */
    char   version[16];
    int    code;
    char   reason[64];

    int matched = sscanf(p->hdr_buf, "%15s %d %63[^\r\n]",
                         version, &code, reason);
    *line_end = '\r'; /* restore */

    if (matched < 2)          { return HTTP_ERR_PARSE; }
    if (code < 100 || code > 599) { return HTTP_ERR_PARSE; }

    resp->status_code = (uint16_t)code;
    return HTTP_OK;
}

/* ── Headers: scan for Content-Length ───────────────────────────────────── */

static http_err_t parse_headers(parser_t *p)
{
    /*
     * hdr_buf is null-terminated at the \r\n\r\n boundary.
     * Walk line by line after the status line.
     */
    char *cursor = strstr(p->hdr_buf, CRLF);
    if (cursor == NULL) { return HTTP_OK; } /* no headers — fine */
    cursor += 2; /* skip past first CRLF */

    while (*cursor != '\0') {
        char *line_end = strstr(cursor, CRLF);
        if (line_end == NULL) { break; }

        *line_end = '\0'; /* temporarily terminate line */

        /* Content-Length */
        if (strncasecmp(cursor, "Content-Length:", 15) == 0) {
            const char *val = cursor + 15;
            while (*val == ' ') { val++; }
            p->content_length = atoi(val);
        }

        /* Transfer-Encoding: chunked (noted, not implemented in this rev) */
        if (strncasecmp(cursor, "Transfer-Encoding:", 18) == 0) {
            const char *val = cursor + 18;
            while (*val == ' ') { val++; }
            if (strncasecmp(val, "chunked", 7) == 0) {
                p->chunked = 1;
            }
        }

        *line_end = '\r'; /* restore */
        cursor = line_end + 2;
    }

    return HTTP_OK;
}