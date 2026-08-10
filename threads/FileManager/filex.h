#ifndef __FILEX_H__
#define __FILEX_H__

#include "fx_api.h"
#include <stdint.h>

typedef enum {
    MEDIA_NOMEDIA = 0,
    MEDIA_SDCARD,
    MEDIA_NOR
} MEDIA;

void FileX_Init();
ULONG FileX_ReadFile(MEDIA media, char* filename, uint8_t* buffer, ULONG* bytes_read);
ULONG FileX_WriteFile(MEDIA media, char* filename, const uint8_t* buffer, uint16_t length);
ULONG FileX_DeleteFile(MEDIA media, char* filename);
ULONG FileX_AppendFile(MEDIA media, char* filename, const uint8_t* buffer, uint16_t length);
ULONG FileX_CreateFile(MEDIA media, char* filename);
ULONG FileX_RenameFile(MEDIA media, char* oldFilename, char* newFilename);
ULONG FileX_CreateFolder(MEDIA media, char* foldername);
ULONG FileX_DeleteFolder(MEDIA media, char* foldername);
ULONG FileX_RenameFolder(MEDIA media, char* oldFoldername, char* newFoldername);
ULONG FileX_GetFileSize(MEDIA media, char* filename, ULONG* size);
ULONG FileX_TestFolder(MEDIA media, char* foldername);
void FileX_Flush(MEDIA media);
#endif