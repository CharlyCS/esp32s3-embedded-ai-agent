idf_component_register(
    SRCS
        "app_main.c"
        "time_sync.c"
        "wifi_manager.c"
    INCLUDE_DIRS "."
    REQUIRES
        aws
        esp_event
        esp_netif
        esp_wifi
        mcp-client
        mcp-server
        nvs_flash
)

spiffs_create_partition_image(
    storage
    "${PROJECT_DIR}/data"
    FLASH_IN_PROJECT
)
