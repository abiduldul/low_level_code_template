#include "App.h"

#include "rtos.h"

#include <stdio.h>

#include "log.h"

static TX_THREAD app_thread;

#ifndef SELECTED_APP_NAME
#error "Select the app first!"
#endif

static void app_entry_function(ULONG);

void App_Init(App_t* app) {
    printf("App: %s, version: %s\r\n", app->app_name, app->app_version);

    app->app_init();

    if(RTOS_CreateThread(&app_thread, app->app_name, app_entry_function, 8192, 4, 1) != TX_SUCCESS) {
        printf("Error initializing app\r\n");
    }

    return;
}

void App_Sleep(uint32_t ms) {
    tx_thread_sleep(ms);

    return;
}

static void app_entry_function(ULONG input) {
    app_active->app_function(&app_active);

    return;
}