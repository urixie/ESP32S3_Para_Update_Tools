#ifndef __STORAGE_FLASH_H
#define __STORAGE_FLASH_H

#include "esp_err.h"

typedef struct {
    char disk_path[128];
} STORAGE_PROC_T;

extern STORAGE_PROC_T g_storage;

extern uint8_t storage_flash_list_files(void);
extern esp_err_t storage_flash_mount(const char *base_path);
extern esp_err_t storage_flash_unmount(const char *base_path);

#endif
