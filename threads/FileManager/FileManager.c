#include "FileManager.h"

#include "filex.h"
#include "rtos.h"
#include "log.h"

static TX_THREAD thread_filemanager;
static TX_QUEUE queue_filemanager;

static void filemanager_thread(ULONG);

typedef enum{
    FILEMANAGER_REQUEST_READ_FILE = 0,
    FILEMANAGER_REQUEST_WRITE_FILE,
    FILEMANAGER_REQUEST_RENAME_FILE,
    FILEMANAGER_REQUEST_CREATE_FILE,
    FILEMANAGER_REQUEST_APPEND_FILE,
    FILEMANAGER_REQUEST_DELETE_FILE,
    FILEMANAGER_REQUEST_GET_FILE_SIZE,
    FILEMANAGER_REQUEST_DELETE_FOLDER,
    FILEMANAGER_REQUEST_CREATE_FOLDER,
    FILEMANAGER_REQUEST_RENAME_FOLDER
} FILEMANAGER_REQUEST;

typedef struct {
    FILEMANAGER_REQUEST type;
    MEDIA media;
    uint8_t status;
    uint8_t async;
    uint8_t* read_ptr;
    const char* name;
    const uint8_t* write_ptr;
    uint32_t* length;
    TX_SEMAPHORE semphr_sync;
} FileManagerRequest_t;

static uint8_t filemanager_request(FILEMANAGER_REQUEST type, FILEMANAGER_MEDIA media, const char* name,  
    uint8_t* read_ptr, const uint8_t* write_ptr, uint32_t* length, uint8_t async) {
    FileManagerRequest_t* request = malloc(sizeof(FileManagerRequest_t));

    request->read_ptr = read_ptr;
    request->write_ptr = write_ptr;
    request->type = type;
    request->media = media;
    request->length = length;
    request->name = name;
    request->async = async;
    request->status = 255;
    
    if(!async) tx_semaphore_create(&request->semphr_sync, "req sync", 0);

    if(tx_queue_send(&queue_filemanager, &request, TX_WAIT_FOREVER) != TX_SUCCESS) {
        if(!async) tx_semaphore_delete(&request->semphr_sync);

        return -1;
    }

    if(async) return 1;

    tx_semaphore_get(&request->semphr_sync, TX_WAIT_FOREVER);

    uint8_t status = request->status;

    tx_semaphore_delete(&request->semphr_sync);

    free(request);

    return status;
}

uint8_t FileManager_Init() {
    uint8_t ret;

    LOG_INFO("Initialize FileX");

    FileX_Init();

    LOG_INFO("Creating the FileManager Thread");

    if((ret = RTOS_CreateThread(&thread_filemanager, "FileManager", filemanager_thread, 
        4096, 5, TX_AUTO_START)) != TX_SUCCESS) {
        LOG_ERROR("Thread initialization error, status = 0x%02X", ret);

        return ret;
    }

    LOG_INFO("FileManager Thread has been created");

    LOG_INFO("Creating the FileManager Queue");

    if((ret = RTOS_CreateQueue(&queue_filemanager, "Queue FileManager", 32)) != TX_SUCCESS) {
        LOG_ERROR("Queue initialization error, status = 0x%02X", ret);

        return ret;
    }

    return 1;
}

uint8_t FileManager_CreateFile(FILEMANAGER_MEDIA media, const char* filename) {
    return filemanager_request(FILEMANAGER_REQUEST_CREATE_FILE, media, filename, NULL, NULL, 0, 0);
}

uint8_t FileManager_WriteFile(FILEMANAGER_MEDIA media, const char* filename, 
    const uint8_t* src, uint32_t* length) {
    return filemanager_request(FILEMANAGER_REQUEST_WRITE_FILE, media, filename, NULL, src, length, 0);
}

uint8_t FileManager_ReadFile(FILEMANAGER_MEDIA media, const char* filename, uint8_t* dst, 
    uint32_t* length) {
    return filemanager_request(FILEMANAGER_REQUEST_READ_FILE, media, filename, dst, 
        NULL, length, 0);
}

uint32_t FileManager_GetFileSize(FILEMANAGER_MEDIA media, const char* filename) {
    uint32_t size = 0;
    
    filemanager_request(FILEMANAGER_REQUEST_GET_FILE_SIZE, media, filename, NULL, NULL,
    &size, 0);

    return size;
}

uint8_t FileManager_AppendFile(FILEMANAGER_MEDIA media, const char* filename, 
    const uint8_t* src, uint32_t* length) {
    return filemanager_request(FILEMANAGER_REQUEST_APPEND_FILE, media, filename, NULL, src, length, 0);
}

static void filemanager_requesthandler(FileManagerRequest_t* request) {
    uint16_t filename_length = strlen(request->name);

    char filename_tmp[filename_length + 1];
    filename_tmp[filename_length] = 0;

    memcpy(filename_tmp, request->name, filename_length + 1);

    switch(request->type) {
        case FILEMANAGER_REQUEST_APPEND_FILE:
        request->status = FileX_AppendFile(request->media, filename_tmp, 
            request->write_ptr, *request->length);

        break;
        case FILEMANAGER_REQUEST_CREATE_FILE:
        request->status = FileX_CreateFile(request->media, filename_tmp);

        break;
        case FILEMANAGER_REQUEST_CREATE_FOLDER:
        request->status = FileX_CreateFolder(request->media, filename_tmp);

        break;
        case FILEMANAGER_REQUEST_DELETE_FILE:
        request->status = FileX_DeleteFile(request->media, filename_tmp);

        break;
        case FILEMANAGER_REQUEST_DELETE_FOLDER:
        request->status = FileX_DeleteFolder(request->media, filename_tmp);

        break;
        case FILEMANAGER_REQUEST_GET_FILE_SIZE:
        request->status = FileX_GetFileSize(request->media, filename_tmp, (ULONG*)request->length);
        
        break;
        case FILEMANAGER_REQUEST_READ_FILE:
        request->status = FileX_ReadFile(request->media, filename_tmp, request->read_ptr, (ULONG*)request->length);
        
        break;
        case FILEMANAGER_REQUEST_RENAME_FILE:
        request->status = FileX_RenameFile(request->media, filename_tmp, (char*)request->write_ptr);

        break;
        case FILEMANAGER_REQUEST_WRITE_FILE:
        request->status = FileX_WriteFile(request->media, filename_tmp, request->write_ptr, *request->length);
        
        break;
        case FILEMANAGER_REQUEST_RENAME_FOLDER:
        request->status = FileX_RenameFolder(request->media, filename_tmp, (char*)request->write_ptr);

        break;
    }

    if(request->status == FX_SUCCESS) {
        LOG_INFO("Operation success");

        request->status = 1;
    } else {
        LOG_ERROR("Operation failed, status = %02x", request->status);
    }

    FileX_Flush(request->media);

    return;
}

static void filemanager_thread(ULONG input) {
    LOG_INFO("FileManager thread has been launched");

    while(1) {
        FileManagerRequest_t* received_req;

        if(tx_queue_receive(&queue_filemanager, &received_req, 
            TX_WAIT_FOREVER) == TX_SUCCESS) {
            filemanager_requesthandler(received_req);

            if(received_req->async) {
                free(received_req);
            } else {
                tx_semaphore_put(&received_req->semphr_sync);
            }
        }
    }

    return;
}