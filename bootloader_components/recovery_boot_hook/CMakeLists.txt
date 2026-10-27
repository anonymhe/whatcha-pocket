idf_component_register(
    SRCS "recovery_boot_hook.c"
    PRIV_REQUIRES bootloader_support
    PRIV_INCLUDE_DIRS "${IDF_PATH}/components/bootloader_support/private_include"
)
