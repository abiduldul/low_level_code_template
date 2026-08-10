#include "log.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static void (*_log_process)(const char*, uint16_t) = NULL;
static time_t (*_rtc_get)(void) = NULL;

void Log_Init(void (*log_process)(const char*, uint16_t), time_t (*rtc_get)(void)){
	_log_process = log_process;
    _rtc_get = rtc_get;

	return;
}

#ifdef LOG_FULL
void Log_Put(const char* function_name, const uint8_t level, int line, const char* fmt, ...) {
    time_t now;
    struct tm timeinfo;
    char time_str[32];
    char* buffer;

    now = _rtc_get();

    localtime_r(&now, &timeinfo);
    strftime(time_str, sizeof(time_str), "[%d/%m/%Y %H:%M:%S]", &timeinfo);

    const char* level_str;
    switch(level) {
        case 0: level_str = "INFO "; break;
        case 1: level_str = "WARN "; break;
        default: level_str = "ERROR"; break;
    }

    printf("%s %s: %s().%d: ", time_str, level_str, function_name, line);

    /*buffer = malloc(512);
    if(buffer == NULL) return;

    uint16_t length_header = snprintf(buffer, 512, "%s %s: %s().%d: ", time_str, level_str, function_name, line);
    
    va_list args;
    va_start(args, fmt);
    uint16_t length_content = vsnprintf(buffer + length_header, 512 - length_header, fmt, args);
    va_end(args);

    memcpy(buffer + length_header + length_content, "\r\n", 2);

    if(_log_process != NULL) _log_process(buffer, length_header + 
        length_content + 2);

    free(buffer);*/

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\r\n");

    return;
}
#else
void Log_Put(const uint8_t level, const char* fmt, ...) {
    va_list args;
    time_t now;
    struct tm *timeinfo;
    static char tmp[2048];
    char time_str[32];

    time(&now);
    timeinfo = localtime(&now);
    strftime(time_str, sizeof(time_str), "[%d/%m/%Y %H:%M:%S]", timeinfo);

    const char *level_str = (level == 0 ? "INFO" : level == 1 ? "WARN" : "ERROR");

    va_start(args, fmt);
    vsnprintf((char*)tmp, 2048, fmt, args);
    va_end(args);

    printf("%s %s: %s\r\n", time_str, level_str, tmp);

    return;
}
#endif