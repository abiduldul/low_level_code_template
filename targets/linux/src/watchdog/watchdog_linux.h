#ifndef WATCHDOG_H
#define WATCHDOG_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Signals                                                              */
/* ------------------------------------------------------------------ */

/* Main program → watchdog: periodic kick to reset countdown */
#define WDT_KICK_SIGNAL     SIGUSR1

/*
 * Main program → watchdog: "I have written my config, start counting."
 * Watchdog will not begin the countdown until this signal is received.
 * This mirrors MCU behaviour: IWDG doesn't count until you write the
 * start key to IWDG_KR.
 */
#define WDT_START_SIGNAL    SIGUSR2

/*
 * Main program → watchdog: pause the countdown.
 * Countdown freezes; kicks are ignored while stopped.
 * Models an MCU entering debug halt or stop mode where IWDG is paused.
 * Resume with wdt_start() to re-arm with the same timeout.
 *
 * SIGRTMIN is the first POSIX real-time signal — guaranteed not to
 * conflict with SIGUSR1/SIGUSR2 or any standard signal.
 */
#define WDT_STOP_SIGNAL     SIGRTMIN

/*
 * Main program → watchdog: trigger an immediate system reset.
 * Does not wait for the countdown — kills the child and restarts
 * immediately. Models NVIC_SystemReset() or a deliberate hard reset.
 */
#define WDT_RESET_SIGNAL    (SIGRTMIN + 1)

/* ------------------------------------------------------------------ */
/* Shared memory layout                                                 */
/* ------------------------------------------------------------------ */

#define WDT_SHM_NAME        "/wdt_config"

/*
 * wdt_shm_t — written by the main program, read by the watchdog.
 *
 * The main program fills this struct and then calls wdt_start() which
 * sends WDT_START_SIGNAL. The watchdog reads it exactly once at that
 * point and never writes to it again.
 */
typedef struct {
    int timeout_sec;    /* Watchdog countdown window in seconds (>0) */
} wdt_shm_t;

/* ------------------------------------------------------------------ */
/* Default timeout (used by watchdog if no config was written)         */
/* ------------------------------------------------------------------ */
#define WDT_DEFAULT_TIMEOUT_SEC  5

/* ------------------------------------------------------------------ */
/* Client API — call these from the main program                       */
/* ------------------------------------------------------------------ */

/*
 * wdt_configure() — set the watchdog timeout before starting it.
 *
 * Writes `timeout_sec` into shared memory so the watchdog can read it.
 * Must be called before wdt_start(). Safe to call multiple times;
 * only the last value before wdt_start() takes effect.
 *
 * Returns 0 on success, -1 on error.
 */
static inline int wdt_configure(int timeout_sec)
{
    if (timeout_sec <= 0) {
        fprintf(stderr, "[APP] wdt_configure: timeout must be > 0\n");
        return -1;
    }

    /* Open (or create) the shared memory segment */
    int fd = shm_open(WDT_SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        perror("[APP] wdt_configure: shm_open");
        return -1;
    }

    if (ftruncate(fd, sizeof(wdt_shm_t)) < 0) {
        perror("[APP] wdt_configure: ftruncate");
        close(fd);
        return -1;
    }

    wdt_shm_t *shm = mmap(NULL, sizeof(wdt_shm_t),
                           PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (shm == MAP_FAILED) {
        perror("[APP] wdt_configure: mmap");
        return -1;
    }

    shm->timeout_sec = timeout_sec;
    munmap(shm, sizeof(wdt_shm_t));

    fprintf(stderr, "[APP] watchdog is configured\r\n");

    return 0;
}

/*
 * wdt_start() — arm the watchdog.
 *
 * Sends WDT_START_SIGNAL to the watchdog (parent process), telling it
 * to read the shared memory config and begin the countdown.
 * Call this once after wdt_configure() and before your main loop.
 *
 * Returns 0 on success, -1 on error.
 */
static inline int wdt_start(void)
{
    pid_t wdt_pid = getppid();
    if (wdt_pid <= 1) {
        fprintf(stderr, "[APP] wdt_start: no watchdog parent\n");
        return -1;
    }
    fprintf(stderr, "[APP] watchdog is started\r\n");
    return kill(wdt_pid, WDT_START_SIGNAL);
}

/*
 * wdt_kick() — reset the watchdog countdown.
 *
 * Call this periodically within your main loop, more frequently than
 * the configured timeout, to prevent a watchdog reset.
 *
 * Returns 0 on success, -1 on error.
 */
static inline int wdt_kick(void)
{
    pid_t wdt_pid = getppid();
    if (wdt_pid <= 1) {
        /* Parent is init — watchdog already exited, skip */
        return -1;
    }
    fprintf(stderr, "[APP] watchdog is kicked\r\n");

    return kill(wdt_pid, WDT_KICK_SIGNAL);
}

/*
 * wdt_stop() — pause the watchdog countdown.
 *
 * The countdown freezes and kicks are ignored. The watchdog enters
 * IDLE state. Call wdt_start() again to re-arm with the same timeout.
 *
 * Typical use: entering a known-long operation (e.g. flash erase)
 * where you cannot kick, but a reset would be wrong.
 *
 * Returns 0 on success, -1 on error.
 */
static inline int wdt_stop(void)
{
    pid_t wdt_pid = getppid();
    if (wdt_pid <= 1) {
        return -1;
    }
    fprintf(stderr, "[APP] watchdog is stopped\r\n");

    return kill(wdt_pid, WDT_STOP_SIGNAL);
}

/*
 * wdt_reset() — trigger an immediate system reset.
 *
 * Does not wait for the countdown to expire. The watchdog will kill
 * the calling process and restart it right away.
 * Use when your application detects an unrecoverable error and wants
 * a clean restart rather than a graceful exit.
 *
 * Note: this function does not return — the process will be killed
 * by the watchdog shortly after the signal is sent.
 *
 * Returns -1 only if the signal could not be sent (no watchdog parent).
 */
static inline int wdt_reset(void)
{
    pid_t wdt_pid = getppid();
    if (wdt_pid <= 1) {
        return -1;
    }
    int ret = kill(wdt_pid, WDT_RESET_SIGNAL);
    if (ret == 0) {
        /*
         * Wait to be killed by the watchdog. pause() suspends until
         * any signal arrives — the watchdog will send SIGKILL shortly,
         * which terminates us without running this code further.
         * This prevents the process from exiting on its own (which
         * would be misread as an unexpected crash rather than a reset).
         */
        while (1) { pause(); }
    }

    fprintf(stderr, "[APP] watchdog is reset\r\n");
    return ret;
}

#endif /* WATCHDOG_H */