#include "storage_flash.h"

#include <dirent.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

STORAGE_PROC_T g_storage = {
    .disk_path = "/disk",
};

static const char *TAG = "storage";
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static bool s_mounted;

uint8_t storage_flash_list_files(void)
{
    DIR *d = opendir(g_storage.disk_path);
    if (d == NULL) {
        ESP_LOGW(TAG, "open dir failed: %s", g_storage.disk_path);
        return 1;
    }

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") != 0 &&
            strcmp(dir->d_name, "..") != 0 &&
            strcmp(dir->d_name, "System Volume Information") != 0) {
            ESP_LOGI("FILE", "%s", dir->d_name);
        }
    }
    closedir(d);
    return 0;
}

esp_err_t storage_flash_mount(const char *base_path)
{
    if (s_mounted) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Mounting FAT filesystem at %s", base_path);
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 9,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
    };

    esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl(base_path, "storage", &mount_config, &s_wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    s_mounted = true;
    return ESP_OK;
}

esp_err_t storage_flash_unmount(const char *base_path)
{
    if (!s_mounted) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Unmounting FAT filesystem at %s", base_path);
    esp_err_t ret = esp_vfs_fat_spiflash_unmount_rw_wl(base_path, s_wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount FATFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    s_wl_handle = WL_INVALID_HANDLE;
    s_mounted = false;
    return ESP_OK;
}
