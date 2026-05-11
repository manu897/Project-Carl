idf_component_register(
    SRCS
        "main.c"
        "wifi_sta.c"
        "mdns_service.c"
        "http_api.c"
        "key_store.c"
        "node_registry.c"
        "ble_scanner.cpp"
        # Shared BTHome library (firmware/common/bthome/) — same code the
        # sensor-node firmware uses on the encode side. C++17.
        "../../common/bthome/bthome.cpp"
        "../../common/bthome/bthome_decode.cpp"
        "../../common/bthome/aes_ccm.cpp"
    INCLUDE_DIRS
        "."
        "../../common"
        "../../common/bthome"
    REQUIRES
        nvs_flash
        bt
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        esp_app_format
        esp_timer
        mbedtls
        json
        mdns
)

# C++17 for the BTHome shared library namespace + the BLE scanner shim.
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
