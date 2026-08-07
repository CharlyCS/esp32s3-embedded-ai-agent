idf_component_register(
    SRCS
        "mcp_server_service.c"
        "mcp_tools.c"
        "audio_service.c"
        "storage.c"
        "tca9555.c"
        "wav_reader.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_codec_dev
        esp_http_server
        esp_timer
        json
        mcp-c-sdk
        spiffs
)
