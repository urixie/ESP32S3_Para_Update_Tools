#include "app_web_file_meta.h"

#include <stddef.h>
#include <string.h>
#include <strings.h>

const char *content_type_from_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext != NULL) {
        if (strcasecmp(ext, ".png") == 0) {
            return "image/png";
        }
        if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
            return "image/jpeg";
        }
        if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) {
            return "text/html; charset=utf-8";
        }
        if (strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".log") == 0) {
            return "text/plain; charset=utf-8";
        }
    }
    return "application/octet-stream";
}
