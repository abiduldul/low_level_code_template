/*
 * watchdog.c — Linux Software Watchdog
 *
 * _POSIX_C_SOURCE exposes sigaction, sigemptyset, kill, waitpid, shm_open, etc.
 * Must be defined before any system header is included.
 */
#define _POSIX_C_SOURCE 200809L

/*
 * Architecture:
 *   watchdog (this program)
 *     └─ fork + exec ──► main_program (child)
 *                            │
 *                            ├─ wdt_configure(N)  → writes N to shared memory
 *                            ├─ wdt_start()       → SIGUSR2: read shm, begin countdown
 *                            ├─ wdt_kick()        → SIGUSR1: reset countdown
 *                            ├─ wdt_stop()        → SIGRTMIN:   freeze countdown
 *                            └─ wdt_reset()       → SIGRTMIN+1: immediate reset
 *
 * Watchdog states:
 *   IDLE    — child running, wdt_start() not yet received. No ticking.
 *   RUNNING — countdown active. Kick resets it; timeout triggers reset.
 *   STOPPED — paused via wdt_stop(). No ticking, kicks ignored.
 *             wdt_start() re-arms from the full timeout value.
 *
 * On timeout or wdt_reset():
 *   1. Child is killed (SIGKILL).
 *   2. Shared memory segment is unlinked.
 *   3. Watchdog exec()'s a fresh copy of itself → re-spawns target.
 *
 * On child clean exit (code 0):
 *   Watchdog unlinks shared memory and exits cleanly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>

#include "watchdog_linux.h"

/* ------------------------------------------------------------------ */
/* Watchdog states                                                      */
/* ------------------------------------------------------------------ */

typedef enum {
    WDT_STATE_IDLE = 0,  /* waiting for wdt_start() from child  */
    WDT_STATE_RUNNING,   /* countdown active                     */
    WDT_STATE_STOPPED,   /* paused via wdt_stop(); not counting  */
} wdt_state_t;

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */

/* PID of the spawned child (main program). */
static volatile pid_t g_child_pid = -1;

/* Current watchdog state. Written by signal handler, read by main loop. */
static volatile atomic_int g_state = WDT_STATE_IDLE;

/*
 * Countdown in seconds. Decremented each tick by the main loop.
 * Reset to g_timeout_sec by the SIGUSR1 (kick) handler.
 */
static volatile atomic_int g_countdown = WDT_DEFAULT_TIMEOUT_SEC;

/* Configured timeout, read once from shared memory on wdt_start(). */
static int g_timeout_sec = WDT_DEFAULT_TIMEOUT_SEC;

/* argv[0] of this watchdog binary — used for self-exec on reset */
static char *g_self_path    = NULL;
/* argv[1..] forwarded to the target program */
static char **g_target_argv = NULL;

/* ------------------------------------------------------------------ */
/* Signal handlers                                                      */
/* ------------------------------------------------------------------ */

/*
 * handle_kick() — SIGUSR1
 * Called when the main program kicks the watchdog.
 * Resets the countdown; ignored when in IDLE state.
 */
static void handle_kick(int sig)
{
    (void)sig;
    if (atomic_load(&g_state) == WDT_STATE_RUNNING) {
        atomic_store(&g_countdown, g_timeout_sec);
    }
}

/*
 * handle_start() — SIGUSR2
 * Called when the main program calls wdt_start().
 * Reads timeout from shared memory, arms the countdown.
 */
static void handle_start(int sig)
{
    (void)sig;

    /* Read timeout from shared memory */
    int fd = shm_open(WDT_SHM_NAME, O_RDONLY, 0);
    if (fd >= 0) {
        wdt_shm_t shm;
        if (read(fd, &shm, sizeof(shm)) == sizeof(shm) && shm.timeout_sec > 0) {
            g_timeout_sec = shm.timeout_sec;
        }
        close(fd);
    }
    /* Unlink immediately — no longer needed after reading */
    shm_unlink(WDT_SHM_NAME);

    atomic_store(&g_countdown, g_timeout_sec);
    atomic_store(&g_state, WDT_STATE_RUNNING);
}

/*
 * handle_stop() — SIGRTMIN
 * Freezes the countdown. Transitions RUNNING → STOPPED.
 * Kicks are silently ignored while stopped.
 * Call wdt_start() to re-arm from the full timeout.
 */
static void handle_stop(int sig)
{
    (void)sig;
    atomic_store(&g_state, WDT_STATE_STOPPED);
}

/*
 * handle_reset() — SIGRTMIN+1
 * Flags an immediate reset request. The main loop detects this flag
 * and calls reset_system(). We use a flag rather than calling
 * reset_system() directly because execvp() is not async-signal-safe.
 */
static volatile atomic_int g_reset_requested = 0;

static void handle_reset(int sig)
{
    (void)sig;
    atomic_store(&g_reset_requested, 1);
}

/*
 * handle_child() — SIGCHLD
 * Minimal handler; actual reaping done in the main loop via waitpid(WNOHANG).
 */
static void handle_child(int sig)
{
    (void)sig;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void log_wdt(const char *msg)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(stderr, "[WDT %02d:%02d:%02d] %s\n",
            t->tm_hour, t->tm_min, t->tm_sec, msg);
    fflush(stderr);
}

/*
 * spawn_target() — fork + exec the target program.
 * Returns child PID on success, exits on failure.
 */
static pid_t spawn_target(char **target_argv)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("[WDT] fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        /* ---- Child: become the target program ---- */
        execvp(target_argv[0], target_argv);
        perror("[WDT] execvp target");
        exit(EXIT_FAILURE);
    }

    return pid;
}

/*
 * reset_system() — watchdog-triggered system reset.
 *
 * Kills the child, cleans up shared memory, then exec()'s a fresh
 * copy of this watchdog. The new instance will re-spawn the target.
 */
static void reset_system(void)
{
    log_wdt("*** WATCHDOG TIMEOUT — RESETTING SYSTEM ***");

    if (g_child_pid > 0) {
        fprintf(stderr, "[WDT] Killing child PID %d\n", (int)g_child_pid);
        fflush(stderr);
        kill(g_child_pid, SIGKILL);
        waitpid(g_child_pid, NULL, 0);
    }

    /* Clean up shared memory in case child never called wdt_start() */
    shm_unlink(WDT_SHM_NAME);

    log_wdt("Restarting watchdog + target...\n");
    fflush(stderr);

    /*
     * Re-exec this watchdog with the same target argument.
     * The fresh watchdog instance will spawn the target again from scratch,
     * exactly like a hardware WDT reset reboots the MCU.
     */
    execvp(g_self_path, (char *const []){ g_self_path, g_target_argv[0], NULL });

    perror("[WDT] execvp self");
    exit(EXIT_FAILURE);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <target_program> [args...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    g_self_path    = argv[0];
    g_target_argv  = &argv[1];

    /* -- Install signal handlers -- */
    struct sigaction sa = {0};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sa.sa_handler = handle_kick;
    sigaction(WDT_KICK_SIGNAL, &sa, NULL);

    sa.sa_handler = handle_start;
    sigaction(WDT_START_SIGNAL, &sa, NULL);

    sa.sa_handler = handle_stop;
    sigaction(WDT_STOP_SIGNAL, &sa, NULL);

    sa.sa_handler = handle_reset;
    sigaction(WDT_RESET_SIGNAL, &sa, NULL);

    sa.sa_handler = handle_child;
    sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    /* -- Spawn target; watchdog starts in IDLE state -- */
    atomic_store(&g_state,     WDT_STATE_IDLE);
    atomic_store(&g_countdown, WDT_DEFAULT_TIMEOUT_SEC);

    g_child_pid = spawn_target(g_target_argv);

    fprintf(stderr, "[WDT] Target spawned. PID=%d. Waiting for wdt_start()...\n",
            (int)g_child_pid);
    fflush(stderr);

    /* -- Main watchdog loop -- */
    while (1) {
        sleep(1);

        /* Check if child exited on its own */
        int status;
        pid_t dead = waitpid(g_child_pid, &status, WNOHANG);
        if (dead == g_child_pid) {
            shm_unlink(WDT_SHM_NAME);

            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                log_wdt("Target exited cleanly. Watchdog shutting down.");
                return EXIT_SUCCESS;
            } else {
                fprintf(stderr, "[WDT] Target died unexpectedly (status=%d). Resetting.\n",
                        WEXITSTATUS(status));
                g_child_pid = -1;
                reset_system();
            }
        }

        /* Immediate reset requested by child via wdt_reset() */
        if (atomic_load(&g_reset_requested)) {
            atomic_store(&g_reset_requested, 0);
            log_wdt("Immediate reset requested by application.");
            reset_system();
        }

        wdt_state_t state = atomic_load(&g_state);

        if (state == WDT_STATE_IDLE) {
            //fprintf(stderr, "[WDT] Idle    — waiting for wdt_start()\n");
            //fflush(stderr);
            continue;
        }

        if (state == WDT_STATE_STOPPED) {
            //fprintf(stderr, "[WDT] Stopped — countdown paused\n");
            //fflush(stderr);
            continue;
        }

        /* WDT_STATE_RUNNING — tick and check */
        int remaining = atomic_fetch_sub(&g_countdown, 1) - 1;
        //fprintf(stderr, "[WDT] Tick — %ds remaining (timeout=%ds)\n",
        //        remaining, g_timeout_sec);
        //fflush(stderr);

        if (remaining <= 0) {
            reset_system();
        }
    }

    return EXIT_SUCCESS;
}