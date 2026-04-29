# Sources are added in Phase 5 of the plan.
idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."
        "../../common"
    REQUIRES
        nvs_flash
        bt
        esp_timer
        mbedtls
        fatfs
        sdmmc
        esp32-camera
        json
)
