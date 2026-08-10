#include "fx_api.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "log.h"

#define SECTOR_SIZE 512
#define TOTAL_SECTORS 65536

static int media_fd = -1;
static char* sdcard_media_filename = "sdcard.img";

VOID filex_media_driver(FX_MEDIA* media_ptr) {
    off_t offset;
    ssize_t bytes_transferred;

    switch(media_ptr->fx_media_driver_request) {
        case FX_DRIVER_INIT:
            media_fd = open(sdcard_media_filename, O_RDWR);

            if(media_fd < 0) {
                media_fd = open(sdcard_media_filename, O_RDWR | O_CREAT, 0666);

                if(media_fd < 0) {
                    LOG_ERROR("Failed to create the file");
                    media_ptr->fx_media_driver_status = FX_IO_ERROR;

                    return;
                }
            }

            off_t file_size = (off_t)TOTAL_SECTORS * SECTOR_SIZE;

            if(posix_fallocate(media_fd, 0, file_size) != 0) {
                close(media_fd);
                
                media_fd = -1;
                media_ptr->fx_media_driver_status = FX_IO_ERROR;
                return;
            }

            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;

        case FX_DRIVER_READ:
            offset = (off_t)media_ptr->fx_media_driver_logical_sector * SECTOR_SIZE;
            lseek(media_fd, offset, SEEK_SET);

            bytes_transferred = read(media_fd, media_ptr->fx_media_driver_buffer,
                media_ptr->fx_media_driver_sectors * SECTOR_SIZE);
            
            if(bytes_transferred == (media_ptr->fx_media_driver_sectors * SECTOR_SIZE))
                media_ptr->fx_media_driver_status = FX_SUCCESS;
            else 
                media_ptr->fx_media_driver_status = FX_IO_ERROR;
                
            break;
        
        case FX_DRIVER_WRITE:
            offset = (off_t)media_ptr->fx_media_driver_logical_sector * SECTOR_SIZE;
            lseek(media_fd, offset, SEEK_SET);

            bytes_transferred = write(media_fd, media_ptr->fx_media_driver_buffer,
                media_ptr->fx_media_driver_sectors * SECTOR_SIZE);
                
            if(bytes_transferred == (media_ptr->fx_media_driver_sectors * SECTOR_SIZE))
                media_ptr->fx_media_driver_status = FX_SUCCESS;
            else
                media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;

        case FX_DRIVER_FLUSH:
            if(media_fd >= 0) fsync(media_fd);

            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        
        case FX_DRIVER_UNINIT:
            if(media_fd >= 0) {
                close(media_fd);
                media_fd = -1;
            }

            media_ptr->fx_media_driver_status = FX_SUCCESS;
            break;
        
        case FX_DRIVER_BOOT_READ:
            lseek(media_fd, 0, SEEK_SET);

            bytes_transferred = read(media_fd, 
                media_ptr->fx_media_driver_buffer,
                SECTOR_SIZE);
            
            if (bytes_transferred == SECTOR_SIZE) 
                media_ptr->fx_media_driver_status = FX_SUCCESS;
            else 
                media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        case FX_DRIVER_BOOT_WRITE:
            lseek(media_fd, 0, SEEK_SET);
            
            bytes_transferred = write(media_fd,
                media_ptr->fx_media_driver_buffer,
                SECTOR_SIZE);
                
            if(bytes_transferred == SECTOR_SIZE) 
                media_ptr->fx_media_driver_status = FX_SUCCESS;
            else
                media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;

        default:
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
    }

    return;
}