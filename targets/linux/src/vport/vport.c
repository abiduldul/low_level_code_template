/**
 * vport.c – Virtual Serial Port Access Library (ThreadX Port)
 * =============================================================
 */
#include "vport.h"
#include "tx_api.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define VPORT_IRQ_BUF               4096
#define VPORT_IRQ_THREAD_STACK_SIZE 16384  /* Increased to 16KB */
#define VPORT_IRQ_THREAD_PRIORITY   15

/* Event flag used to synchronize thread teardown without pthread_join */
#define VPORT_EVT_EXITED            0x01
#define MAX_SIMULATED_VPORTS 8

struct vport {
    int              registry_id;  /* NEW: Safe ID to pass to ThreadX */
    
    int              fd;
    int              timeout_ms;
    char             device[PATH_MAX];

    TX_MUTEX         write_lock;

    /* ── IRQ subsystem ─────────────────────────────────────────────────── */
    vport_rx_cb_t    irq_cb;
    void            *irq_user;
    
    TX_THREAD        irq_thread;
    void            *irq_stack;
    TX_EVENT_FLAGS_GROUP irq_events;

    atomic_int       irq_running;
    int              irq_thread_valid;   
    int              irq_pipe[2];        
    TX_MUTEX         irq_mtx;
};

static vport_t *g_vport_registry[MAX_SIMULATED_VPORTS] = {NULL};
/* ── Helpers ───────────────────────────────────────────────────────────── */

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
        case     9600: return B9600;
        case    19200: return B19200;
        case    38400: return B38400;
        case    57600: return B57600;
        case   115200: return B115200;
        case   230400: return B230400;
        case   460800: return B460800;
        case   921600: return B921600;
        case  1000000: return B1000000;
        case  1500000: return B1500000;
        case  2000000: return B2000000;
        default:
            fprintf(stderr, "[vport] unsupported baud %d, using 115200\n", baud);
            return B115200;
    }
}

static int configure_termios(int fd, int baudrate, int timeout_ms)
{
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) { perror("[vport] tcgetattr"); return -1; }

    cfmakeraw(&tio);

    speed_t spd = baud_to_speed(baudrate);
    cfsetispeed(&tio, spd);
    cfsetospeed(&tio, spd);

    tio.c_cflag &= (unsigned)~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    tio.c_cflag |= CS8 | CLOCAL | CREAD;

    if (timeout_ms == 0) {
        tio.c_cc[VMIN]  = 1;
        tio.c_cc[VTIME] = 0;
    } else {
        tio.c_cc[VMIN]  = 0;
        int vtime = (timeout_ms + 99) / 100;
        if (vtime < 1)   vtime = 1;
        if (vtime > 255) vtime = 255;
        tio.c_cc[VTIME] = (cc_t)vtime;
    }

    if (tcsetattr(fd, TCSANOW, &tio) != 0) { perror("[vport] tcsetattr"); return -1; }
    return 0;
}

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

vport_t *vport_open(const char *path, int baudrate)
{
    if (!path) { errno = EINVAL; return NULL; }

    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        fprintf(stderr, "[vport] realpath(%s): %s\n", path, strerror(errno));
        return NULL;
    }

    /* Open non-blocking and LEAVE IT non-blocking */
    int fd = open(resolved, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[vport] open(%s): %s\n", resolved, strerror(errno));
        return NULL;
    }

    if (configure_termios(fd, baudrate, VPORT_DEFAULT_TIMEOUT_MS) != 0) {
        close(fd); return NULL;
    }

    vport_t *p = (vport_t *)calloc(1, sizeof(vport_t));
    if (!p) { close(fd); return NULL; }

    p->fd         = fd;
    p->timeout_ms = VPORT_DEFAULT_TIMEOUT_MS;
    strncpy(p->device, resolved, PATH_MAX - 1);

    tx_mutex_create(&p->write_lock, "vport_wr_mtx", TX_NO_INHERIT);
    tx_mutex_create(&p->irq_mtx, "vport_irq_mtx", TX_NO_INHERIT);
    tx_event_flags_create(&p->irq_events, "vport_irq_evt");

    p->irq_cb           = NULL;
    p->irq_user         = NULL;
    p->irq_stack        = NULL;
    atomic_init(&p->irq_running, 0);
    p->irq_thread_valid = 0;
    p->irq_pipe[0]      = -1;
    p->irq_pipe[1]      = -1;

    /* Find an empty slot in the registry */
    p->registry_id = -1;
    for (int i = 0; i < MAX_SIMULATED_VPORTS; i++) {
        if (g_vport_registry[i] == NULL) {
            g_vport_registry[i] = p;
            p->registry_id = i;
            break;
        }
    }

    if (p->registry_id == -1) {
        fprintf(stderr, "[vport] Maximum simulated vports reached\n");
        free(p);
        close(fd);
        return NULL;
    }

    return p;
}

void vport_close(vport_t *port)
{
    if (!port) return;
    vport_irq_disable(port); 
    
    tx_mutex_delete(&port->write_lock);
    tx_mutex_delete(&port->irq_mtx);
    tx_event_flags_delete(&port->irq_events);
    
    /* Remove from registry */
    if (port->registry_id >= 0 && port->registry_id < MAX_SIMULATED_VPORTS) {
        g_vport_registry[port->registry_id] = NULL;
    }
    
    close(port->fd);
    free(port);
}
/* ── I/O ───────────────────────────────────────────────────────────────── */

int vport_write(vport_t *port, const uint8_t *buf, size_t len)
{
    if (!port || !buf || len == 0) { errno = EINVAL; return -1; }

    tx_mutex_get(&port->write_lock, TX_WAIT_FOREVER);
    size_t written = 0;
    int    rc      = 0;
    
    while (written < len) {
        ssize_t n = write(port->fd, buf + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                tx_thread_sleep(1); /* Yield RTOS to allow buffer to drain */
                continue;
            }
            perror("[vport] write");
            rc = -1;
            break;
        }
        written += (size_t)n;
    }
    tx_mutex_put(&port->write_lock);
    return (rc == 0) ? (int)written : -1;
}

int vport_read(vport_t *port, uint8_t *buf, size_t max_len)
{
    if (!port || !buf || max_len == 0) { errno = EINVAL; return -1; }

    struct timespec deadline = {0, 0};
    int use_deadline = (port->timeout_ms > 0);
    if (use_deadline) {
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec  += port->timeout_ms / 1000;
        deadline.tv_nsec += (port->timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
    }

    for (;;) {
        ssize_t n = read(port->fd, buf, max_len);
        if (n > 0) return (int)n;
        
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (use_deadline) {
                    struct timespec now;
                    clock_gettime(CLOCK_MONOTONIC, &now);
                    if (now.tv_sec > deadline.tv_sec ||
                       (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                        return 0; /* Timeout */
                    }
                }
                tx_thread_sleep(1); /* Yield to RTOS */
                continue;
            }
            perror("[vport] read");
            return -1;
        }
        
        return 0; /* EOF */
    }
}

int vport_read_exact(vport_t *port, uint8_t *buf, size_t len)
{
    if (!port || !buf || len == 0) { errno = EINVAL; return -1; }

    struct timespec deadline = {0, 0};
    int use_deadline = (port->timeout_ms > 0);
    if (use_deadline) {
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec  += port->timeout_ms / 1000;
        deadline.tv_nsec += (port->timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
    }

    size_t total = 0;
    while (total < len) {
        ssize_t n = read(port->fd, buf + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (use_deadline) {
                    struct timespec now;
                    clock_gettime(CLOCK_MONOTONIC, &now);
                    if (now.tv_sec > deadline.tv_sec ||
                       (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                        return (int)total; /* Timeout */
                    }
                }
                tx_thread_sleep(1); /* Yield to RTOS */
                continue;
            }
            perror("[vport] read_exact");
            return -1;
        }
        if (n == 0) return (int)total; /* EOF */
        total += (size_t)n;
    }
    return (int)total;
}

/* ── Configuration ─────────────────────────────────────────────────────── */

void vport_set_timeout_ms(vport_t *port, int ms)
{
    if (!port) return;
    port->timeout_ms = ms;

    struct termios tio;
    if (tcgetattr(port->fd, &tio) != 0) return;

    if (ms == 0) {
        tio.c_cc[VMIN]  = 1;
        tio.c_cc[VTIME] = 0;
    } else {
        tio.c_cc[VMIN]  = 0;
        int vtime = (ms + 99) / 100;
        if (vtime < 1)   vtime = 1;
        if (vtime > 255) vtime = 255;
        tio.c_cc[VTIME] = (cc_t)vtime;
    }
    tcsetattr(port->fd, TCSANOW, &tio);
}

void vport_flush(vport_t *port, int queue)
{
    if (port) tcflush(port->fd, queue);
}

int vport_fd(const vport_t *port)             { return port ? port->fd : -1; }
const char *vport_device(const vport_t *port) { return port ? port->device : NULL; }

/* ═══════════════════════════════════════════════════════════════════════════
 * IRQ subsystem (ThreadX Implementation)
 * ═══════════════════════════════════════════════════════════════════════════ */

static VOID irq_listener(ULONG arg)
{
    /* CRITICAL: Retrieve the pointer safely from the registry */
    int id = (int)arg;
    vport_t *p = g_vport_registry[id];
    
    if (!p) {
        tx_thread_suspend(tx_thread_identify());
        return;
    }
    
    /* Allocate buffer on the heap to remove pressure from the ThreadX stack */
    uint8_t *buf = malloc(VPORT_IRQ_BUF);
    if (!buf) {
        atomic_store(&p->irq_running, 0);
        tx_event_flags_set(&p->irq_events, VPORT_EVT_EXITED, TX_OR);
        tx_thread_suspend(tx_thread_identify());
        return;
    }

    for (;;) {
        uint8_t dummy;
        
        /* 1. Non-blocking check of shutdown pipe */
        if (read(p->irq_pipe[0], &dummy, 1) == 1) {
            break; 
        }

        /* 2. Non-blocking read from serial port */
        ssize_t n = read(p->fd, buf, VPORT_IRQ_BUF);

        if (n > 0) {
            tx_mutex_get(&p->irq_mtx, TX_WAIT_FOREVER);
            vport_rx_cb_t cb   = p->irq_cb;
            void         *user = p->irq_user;
            tx_mutex_put(&p->irq_mtx);

            if (cb) cb(buf, (size_t)n, user);
        } 
        else if (n == 0) {
            fprintf(stderr, "[vport-irq] EOF on %s — master closed\n", p->device);
            break;
        } 
        else {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                /* No data yet. Yield to RTOS scheduler so other threads run */
                tx_thread_sleep(1); 
            } else {
                perror("[vport-irq] read");
                break;
            }
        }
    }

    free(buf);
    atomic_store(&p->irq_running, 0);
    
    /* Signal completion and safely park the ThreadX thread */
    tx_event_flags_set(&p->irq_events, VPORT_EVT_EXITED, TX_OR);
    tx_thread_suspend(tx_thread_identify());
}

/* ── Public IRQ API ────────────────────────────────────────────────────── */

void vport_irq_attach(vport_t *port, vport_rx_cb_t cb, void *user)
{
    if (!port) return;
    tx_mutex_get(&port->irq_mtx, TX_WAIT_FOREVER);
    port->irq_cb   = cb;
    port->irq_user = user;
    tx_mutex_put(&port->irq_mtx);
}

int vport_irq_enable(vport_t *port)
{
    if (!port) return -1;

    tx_mutex_get(&port->irq_mtx, TX_WAIT_FOREVER);

    if (port->irq_thread_valid) {
        tx_mutex_put(&port->irq_mtx);
        return 0;
    }

    if (pipe(port->irq_pipe) != 0) {
        perror("[vport-irq] pipe");
        tx_mutex_put(&port->irq_mtx);
        return -1;
    }

    /* CRITICAL: Both ends of the pipe must be non-blocking to prevent locking the RTOS */
    int fl0 = fcntl(port->irq_pipe[0], F_GETFL);
    fcntl(port->irq_pipe[0], F_SETFL, fl0 | O_NONBLOCK);
    
    int fl1 = fcntl(port->irq_pipe[1], F_GETFL);
    fcntl(port->irq_pipe[1], F_SETFL, fl1 | O_NONBLOCK);

    port->irq_stack = malloc(VPORT_IRQ_THREAD_STACK_SIZE);
    if (!port->irq_stack) {
        close(port->irq_pipe[0]); port->irq_pipe[0] = -1;
        close(port->irq_pipe[1]); port->irq_pipe[1] = -1;
        tx_mutex_put(&port->irq_mtx);
        return -1;
    }

    tx_event_flags_set(&port->irq_events, ~VPORT_EVT_EXITED, TX_AND);
    atomic_store(&port->irq_running, 1);

    UINT status = tx_thread_create(
        &port->irq_thread,
        "vport_irq_thread",
        irq_listener,
        (ULONG)port->registry_id,  /* CRITICAL: Pass the ID, not the 64-bit pointer */
        port->irq_stack,
        VPORT_IRQ_THREAD_STACK_SIZE,
        VPORT_IRQ_THREAD_PRIORITY,
        VPORT_IRQ_THREAD_PRIORITY,
        TX_NO_TIME_SLICE,
        TX_AUTO_START
    );

    if (status != TX_SUCCESS) {
        fprintf(stderr, "[vport-irq] tx_thread_create failed (0x%02X)\n", status);
        atomic_store(&port->irq_running, 0);
        free(port->irq_stack);    port->irq_stack = NULL;
        close(port->irq_pipe[0]); port->irq_pipe[0] = -1;
        close(port->irq_pipe[1]); port->irq_pipe[1] = -1;
        tx_mutex_put(&port->irq_mtx);
        return -1;
    }

    port->irq_thread_valid = 1;
    tx_mutex_put(&port->irq_mtx);
    return 0;
}

void vport_irq_disable(vport_t *port)
{
    if (!port) return;

    tx_mutex_get(&port->irq_mtx, TX_WAIT_FOREVER);
    int need_join = port->irq_thread_valid;
    tx_mutex_put(&port->irq_mtx);

    if (!need_join) return;

    /* Wake the thread from select() using the self-pipe */
    uint8_t bye = 0xFF;
    if (port->irq_pipe[1] >= 0) {
        (void)write(port->irq_pipe[1], &bye, 1);
    }

    /* Wait for the ThreadX thread to hit the end and signal exit */
    ULONG actual_events;
    tx_event_flags_get(&port->irq_events, VPORT_EVT_EXITED, TX_OR_CLEAR, &actual_events, TX_WAIT_FOREVER);

    /* CRITICAL: Yield the main thread briefly. This allows the OS to unroll the 
       canceled POSIX thread underneath ThreadX before we rip the memory away. */
    tx_thread_sleep(2);

    tx_thread_terminate(&port->irq_thread);
    tx_thread_delete(&port->irq_thread);

    tx_mutex_get(&port->irq_mtx, TX_WAIT_FOREVER);
    port->irq_thread_valid = 0;
    
    if (port->irq_stack) { 
        free(port->irq_stack); 
        port->irq_stack = NULL; 
    }
    tx_mutex_put(&port->irq_mtx);

    if (port->irq_pipe[0] >= 0) { close(port->irq_pipe[0]); port->irq_pipe[0] = -1; }
    if (port->irq_pipe[1] >= 0) { close(port->irq_pipe[1]); port->irq_pipe[1] = -1; }
}

bool vport_irq_running(const vport_t *port)
{
    return port && atomic_load(&port->irq_running);
}