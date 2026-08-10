#ifndef __RTC_H__
#define __RTC_H__

#include <time.h>

void RTC_Init();
time_t RTC_GetTimestamp();
void RTC_SetTimestamp(time_t timestamp);

#endif