/* Includes */
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>


/* Variables */
extern int __io_putchar(int ch) __attribute__((weak));
extern int __io_getchar(void) __attribute__((weak));


char *__env[1] = { 0 };
char **environ = __env;


/* Functions */
void initialise_monitor_handles()
{
}

int _getpid(void)
{
  return 1;
}

int _kill(int pid, int sig)
{
  (void)pid;
  (void)sig;
  errno = EINVAL;
  return -1;
}

void _exit (int status)
{
  _kill(status, -1);
  while (1) {}    /* Make sure we hang here */
}

__attribute__((weak)) int _read(int file, char *ptr, int len)
{
  (void)file;
  int DataIdx;

  for (DataIdx = 0; DataIdx < len; DataIdx++)
  {
    *ptr++ = __io_getchar();
  }

  return len;
}

__attribute__((weak)) int _write(int file, char *ptr, int len)
{
  (void)file;
  int DataIdx;

  for (DataIdx = 0; DataIdx < len; DataIdx++)
  {
    __io_putchar(*ptr++);
  }
  return len;
}

int _close(int file)
{
  (void)file;
  return -1;
}


int _fstat(int file, struct stat *st)
{
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int file)
{
  (void)file;
  return 1;
}

int _lseek(int file, int ptr, int dir)
{
  (void)file;
  (void)ptr;
  (void)dir;
  return 0;
}

int _open(char *path, int flags, ...)
{
  (void)path;
  (void)flags;
  /* Pretend like we always fail */
  return -1;
}

int _wait(int *status)
{
  (void)status;
  errno = ECHILD;
  return -1;
}

int _unlink(char *name)
{
  (void)name;
  errno = ENOENT;
  return -1;
}

int _times(struct tms *buf)
{
  (void)buf;
  return -1;
}

int _stat(char *file, struct stat *st)
{
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _link(char *old, char *new)
{
  (void)old;
  (void)new;
  errno = EMLINK;
  return -1;
}

int _fork(void)
{
  errno = EAGAIN;
  return -1;
}

int _execve(char *name, char **argv, char **env)
{
  (void)name;
  (void)argv;
  (void)env;
  errno = ENOMEM;
  return -1;
}

// This function bridges the standard C library to your specific hardware.
int _gettimeofday(struct timeval *tv, void *tzvp) {
    if (tv == NULL) {
        return -1; // Error
    }

    // ---------------------------------------------------------
    // 1. HARDWARE SPECIFIC BLOCK (Isolate your HAL calls here)
    // ---------------------------------------------------------
    // Read your specific microcontroller's hardware clock here.
    // For example, reading an I2C RTC chip, or the internal STM32 RTC.
    
    // Let's pretend your hardware gave you these raw values:

    
    int hw_year = 2026;
    int hw_month = 3;
    int hw_day = 3;
    int hw_hour = 11;
    int hw_min = 39;
    int hw_sec = 9;
    // ---------------------------------------------------------

    // 2. Convert hardware values to standard 'struct tm'
    struct tm timeinfo = {0};
    timeinfo.tm_year = hw_year - 1900; // Standard C expects years since 1900
    timeinfo.tm_mon  = hw_month - 1;   // Standard C expects months 0-11
    timeinfo.tm_mday = hw_day;         // 1-31
    timeinfo.tm_hour = hw_hour;        // 0-23
    timeinfo.tm_min  = hw_min;         // 0-59
    timeinfo.tm_sec  = hw_sec;         // 0-59

    // 3. Convert 'struct tm' to UNIX Epoch timestamp (seconds since 1970)
    // mktime() is a standard C function that does this math for you.
    tv->tv_sec = mktime(&timeinfo);
    
    // If your hardware supports milliseconds/microseconds, set them here. 
    // Otherwise, just set it to 0.
    tv->tv_usec = 0; 

    return 0; // 0 indicates success to the C library
}