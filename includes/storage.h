#ifndef __FILE_H__
#define __FILE_H__

#include <stdint.h>

typedef enum {
    FILE_STATUS_SUCCESS = 0,
    FILE_STATUS_FAILED
} file_status_t;

typedef struct {
    file_status_t (*read_file)(const char* filename, uint8_t* data, uint16_t length);
    file_status_t (*write_file)(const char* filename, const uint8_t*, uint16_t length);
    file_status_t (*rename_file)(const char* old_filename, const char* new_filename);
    file_status_t (*get_filesize)(const char* get_filesize);
} file_driver_t;

#endif