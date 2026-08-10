#ifndef __FILEMANAGER_H__
#define __FILEMANAGER_H__

#include <stdint.h>

typedef enum {
    FILEMANAGER_MEDIA_SDCARD = 1,
    FILEMANAGER_MEDIA_NOR
} FILEMANAGER_MEDIA;



uint8_t FileManager_Init();
uint8_t FileManager_CreateFile(FILEMANAGER_MEDIA media, const char* filename);
uint8_t FileManager_WriteFile(FILEMANAGER_MEDIA media, const char* filename, 
    const uint8_t* src, uint32_t* length);
uint8_t FileManager_ReadFile(FILEMANAGER_MEDIA media, const char* filename, uint8_t* dst, 
    uint32_t* length);
uint8_t FileManager_AppendFile(FILEMANAGER_MEDIA media, const char* filename, 
    const uint8_t* src, uint32_t* length);
uint32_t FileManager_GetFileSize(FILEMANAGER_MEDIA media, const char* filename);

#endif