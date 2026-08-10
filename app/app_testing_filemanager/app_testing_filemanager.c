#include "FileManager.h"
#include "Logger.h"
#include "log.h"
#include "App.h"

#include <stdio.h>

static void app_testing_init();
static void app_testing_function();

static App_t app_testing_filemanager = {.app_init = app_testing_init, .app_function = app_testing_function, 
    .app_name = "App Testing", .app_version = "1.0.0"};

REGISTER_APP(app_testing_filemanager);

static void app_testing_init(void) {
    Logger_Init();
    
    FileManager_Init();
}

static void app_testing_function() {
    const char* filename = "Test123";
    uint32_t counter = 0;
    uint32_t write_length;
    uint32_t read_length;
    uint8_t buffer_write[256];
    uint8_t buffer_read[256];
    uint32_t filesize = 0;

    LOG_INFO("Test dulu!");

    if((filesize = FileManager_GetFileSize(FILEMANAGER_MEDIA_SDCARD, filename)) == 0) {
        write_length = sprintf((char*)buffer_write, "Aku hanya mencoba\r\n");

        FileManager_WriteFile(FILEMANAGER_MEDIA_SDCARD, filename, buffer_write, 
            &write_length);
    }

    if(FileManager_ReadFile(FILEMANAGER_MEDIA_SDCARD, filename, buffer_read, 
        &read_length) != 1) {
        LOG_ERROR("Read file error");
    } else {
        LOG_INFO("Read file success");
    }

    LOG_INFO("Isi file: %.*s", read_length, buffer_read);

    while(1) {
        LOG_INFO("Counter: %lu", counter++);

        App_Sleep(1000);
    }
}