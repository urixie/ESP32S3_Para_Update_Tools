#ifndef APP_WEB_FILE_SERVER_H
#define APP_WEB_FILE_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_web_file_server_start(const char *mount_point);

#ifdef __cplusplus
}
#endif

#endif
