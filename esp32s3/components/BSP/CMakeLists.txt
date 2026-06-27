set(src_dirs
            .
            STORAGE)

set(include_dirs
            .
            STORAGE)

set(requires
            nvs_flash
            fatfs
            vfs
            app_storage_lock
            )

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O0 -Wno-error=format=-Wno-format -Wno-unused-variable)
