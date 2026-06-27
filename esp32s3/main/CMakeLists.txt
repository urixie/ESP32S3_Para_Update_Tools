
set(src_dirs
            .)

set(include_dirs
            .)

idf_component_register(
    SRC_DIRS ${src_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES BSP app_storage_lock app_wifi app_web_file_server
)
        
