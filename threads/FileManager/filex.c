#include "fx_api.h"
#include "tx_api.h"

#include "filex.h"

#include "log.h"
#include "rtos.h"

#define SECTOR_SIZE 512
#define TOTAL_SECTORS 65536

static FX_MEDIA sdcard_media_handle;
static UCHAR media_memory[512];

static char fx_init_thread_memory[1024];
static TX_THREAD fx_init_thread;

extern VOID filex_media_driver(FX_MEDIA*);

static void thread_fx_media_init(ULONG thread_input);

static FX_MEDIA* medias[] = {NULL, &sdcard_media_handle};

void FileX_Init() {
    LOG_INFO("Initialize FileX");

    tx_thread_create(&fx_init_thread, 
        "Init FileX",
        thread_fx_media_init, 
        0, 
        fx_init_thread_memory, 
        sizeof(fx_init_thread_memory),
        1, 
        1, 
        TX_NO_TIME_SLICE, 
        TX_AUTO_START);

    fx_system_initialize();

    return;
}

void FileX_Flush(MEDIA media) {
    fx_media_flush(medias[media]);

    return;
}

ULONG FileX_ReadFile(MEDIA media, char* filename, uint8_t* buffer, ULONG* bytes_read) {
	FX_FILE file;
	ULONG status;

	if((status = fx_file_open(medias[media], &file, filename, FX_OPEN_FOR_READ)) != FX_SUCCESS) return status;
	if((status = fx_file_seek(&file, 0)) != FX_SUCCESS) return status;
	if((status = fx_file_read(&file, buffer, file.fx_file_current_file_size, bytes_read)) != FX_SUCCESS) return status;
	if((status = fx_file_close(&file)) != FX_SUCCESS) return status;

	return FX_SUCCESS;
}

ULONG FileX_WriteFile(MEDIA media, char* filename, const uint8_t* buffer, uint16_t length) {
	FX_FILE file;
	ULONG status = fx_file_create(medias[media], filename);

	if(status != FX_SUCCESS && status != FX_ALREADY_CREATED) return status;
	if(length == 0) return status;

	if((status = fx_file_open(medias[media], &file, filename, FX_OPEN_FOR_WRITE)) != FX_SUCCESS) return status;
	if((status = fx_file_seek(&file, 0)) != FX_SUCCESS) return status;
	if((status = fx_file_write(&file, (VOID*)buffer, length)) != FX_SUCCESS) return status;
	if((status = fx_file_close(&file)) != FX_SUCCESS) return status;

	return FX_SUCCESS;
}

ULONG FileX_DeleteFile(MEDIA media, char* filename) {
	return fx_file_delete(medias[media], filename);
}

ULONG FileX_AppendFile(MEDIA media, char* filename, const uint8_t* buffer, uint16_t length) {
	FX_FILE file;

	ULONG status = fx_file_create(medias[media], filename);

	if(status != FX_SUCCESS && status != FX_ALREADY_CREATED) return status;
	if(length == 0) return status;

	if((status = fx_file_open(medias[media], &file, filename, FX_OPEN_FOR_WRITE)) != FX_SUCCESS) return status;
	if((status = fx_file_seek(&file, file.fx_file_current_file_size)) != FX_SUCCESS) return status;
	if((status = fx_file_write(&file, (VOID*)buffer, length)) != FX_SUCCESS) return status;
	if((status = fx_file_close(&file)) != FX_SUCCESS) return status;

	return FX_SUCCESS;
}

ULONG FileX_CreateFile(MEDIA media, char* filename) {
	ULONG status;

	if((status = FileX_WriteFile(media, filename, NULL, 0)) != FX_SUCCESS) return status;
	return FX_SUCCESS;
}

ULONG FileX_RenameFile(MEDIA media, char* oldFilename, char* newFilename) {
	ULONG status;

	if((status = fx_file_rename(medias[media], oldFilename, newFilename))
			!= FX_SUCCESS) return status;

	return FX_SUCCESS;
}

ULONG FileX_CreateFolder(MEDIA media, char* foldername) {
	ULONG status;

	status = fx_directory_create(medias[media], foldername);

	return status;
}

ULONG FileX_DeleteFolder(MEDIA media, char* foldername) {
	ULONG status;
	char entry_name[16] = {0};

	if((status = fx_directory_default_set(medias[media], foldername)) != FX_SUCCESS) return status;

	while((status = fx_directory_next_entry_find(medias[media], entry_name)) == FX_SUCCESS) {
		if(entry_name[0] == '.') continue;

		status = fx_file_delete(medias[media], entry_name);
	}

	fx_directory_default_set(medias[media], FX_NULL);

	status = fx_directory_delete(medias[media], foldername);

	return status;
}

ULONG FileX_RenameFolder(MEDIA media, char* oldName,
		char* newName) {
	ULONG status;

	status = fx_directory_rename(medias[media], oldName, newName);

	return status;
}

ULONG FileX_GetFileSize(MEDIA media, char* filename, ULONG* size) {
	FX_FILE file;
	ULONG status;

	if((status = fx_file_open(medias[media], &file, filename, FX_OPEN_FOR_READ))
			!= FX_SUCCESS) return status;

	*size = file.fx_file_current_file_size;

	if((status = fx_file_close(&file)) != FX_SUCCESS) return status;

	return FX_SUCCESS;
}

ULONG FileX_TestFolder(MEDIA media, char* foldername) {
	ULONG status;

	status = fx_directory_name_test(medias[media], foldername);

	return status;
}

static void thread_fx_media_init(ULONG thread_input) {
    UINT status;

    status = fx_media_open(&sdcard_media_handle,
        "SDCARD_DISK",
        filex_media_driver,
        NULL,
        media_memory,
        sizeof(media_memory));
    
    if(status != FX_SUCCESS) {
        LOG_ERROR("Error opening the media, status = 0x%02X", status);

        LOG_INFO("Trying to format the media");

        status = fx_media_format(&sdcard_media_handle, 
            filex_media_driver, 
            NULL,
            media_memory,
            sizeof(media_memory),
            "SDCARD_DISK",
            1,
            32,
            0,
            TOTAL_SECTORS,
            SECTOR_SIZE,
            8,
            1,
            1);

        if(status != FX_SUCCESS) {
            LOG_ERROR("Error formatting the media, status = 0x%02X", status);
        } else {
            LOG_INFO("Media has been formatted successfully");

            LOG_INFO("Opening the media");

            status = fx_media_open(&sdcard_media_handle,
                "SDCARD_DISK",
                filex_media_driver,
                NULL,
                media_memory,
                sizeof(media_memory));
        }
    }

    if(status != FX_SUCCESS) {
        LOG_ERROR("Error opening the media, status = 0x%02X", status);

        return;
    } else {
        LOG_INFO("Opening success");
    }

    fx_media_flush(&sdcard_media_handle);

    return;
}

/*static uint8_t coba_baca_file() {
    UINT status;
    FX_FILE myFile;
    
    char tmpBuffer[128] = {0};
    ULONG actualFileSize;

    if((status = fx_file_open(&sdcard_media_handle, &myFile, "tulis_file.txt", FX_OPEN_FOR_READ)) != FX_SUCCESS) {
        LOG_ERROR("Open File Error, status = 0x%02X", status);

        return -1;
    }
        
    if((status = fx_file_read(&myFile, (uint8_t*)tmpBuffer, sizeof(tmpBuffer), &actualFileSize)) != FX_SUCCESS) {
        LOG_ERROR("Read File Error, status = 0x%02X", status);

        return -1;
    }
    
    fx_file_close(&myFile);

    LOG_INFO("Isi File: %s", tmpBuffer);

    return 1;
}

static uint8_t coba_tulis_file() {
    UINT status;
    FX_FILE myFile;
    
    uint8_t* tulisan = "Ini adalah tulisan dia\r\n";

    if((status = fx_file_create(&sdcard_media_handle, "tulis_file.txt")) != FX_SUCCESS) {
        LOG_ERROR("Create File Error, status = 0x%02X", status);

        if(status != FX_ALREADY_CREATED) return -1;
        else LOG_INFO("File has been already created before");
    }

    if((status = fx_file_open(&sdcard_media_handle, &myFile, "tulis_file.txt", FX_OPEN_FOR_WRITE)) != FX_SUCCESS) {
        LOG_ERROR("Open File Error, status = 0x%02X", status);

        return -1;
    }

    if((status = fx_file_write(&myFile, (uint8_t*)tulisan, strlen(tulisan))) != FX_SUCCESS) {
        LOG_ERROR("Open File Error, status = 0x%02X", status);

        return -1;
    }

    fx_file_close(&myFile);

    LOG_INFO("File has been succesfully created and written");

    return 1;
}*/