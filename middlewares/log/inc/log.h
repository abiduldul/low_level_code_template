#ifndef __LOG_H__
#define __LOG_H__

#define LOG_FULL
#define LOG_ACTIVE

#include <stdint.h>
#include <time.h>

#ifdef LOG_ACTIVE
#define LOG_PRINT(level, ...)
#ifdef LOG_FULL
#define LOG_INFO(...) Log_Put(__FUNCTION__, 0, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) Log_Put(__FUNCTION__, 1, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Log_Put(__FUNCTION__, 2, __LINE__, __VA_ARGS__)
#else
#define LOG_INFO(...) Log_Put(0, __VA_ARGS__)
#define LOG_WARNING(...) Log_Put(1, __VA_ARGS__)
#define LOG_ERROR(...) Log_Put(2, __VA_ARGS__)
#endif
#else
#define LOG_INFO(...)
#define LOG_WARNING(...)
#define LOG_ERROR(...)
#endif

void Log_Init(void (*log_process)(const char*, uint16_t), time_t (*rtc_get)(void));

#ifdef LOG_FULL
void Log_Put(const char* function_name, const uint8_t level, int line, const char* fmt, ...);
#else
void Log_Put(const uint8_t level, const char* fmt, ...);
#endif

#endif