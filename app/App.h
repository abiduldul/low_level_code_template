#ifndef __APP_H__
#define __APP_H__
#include <stdint.h>

#define APP_WAIT_FOREVER    (uint32_t)0xFFFFFFFF

typedef struct App_t{
    const char* const app_name;
    const char* const app_version;
    void (*const app_init)(void);
    void (*const app_function)(const struct App_t* self);
    void* const ctx;
} App_t;

void App_Init(App_t* app);
void App_Sleep(uint32_t ms);

extern const App_t* const app_active;

#define REGISTER_APP(app_struct) const App_t* const app_active = &(app_struct)
#endif