cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(carl_node_sensor LANGUAGES C CXX)

target_sources(app PRIVATE
    src/main.cpp
    src/sensors.cpp
    src/thresholds.cpp
    src/calibration.cpp
    src/keystore.cpp
    src/bthome_emit.cpp
    # Shared library — compiled into the app build (no separate Zephyr module
    # because the file count is tiny).
    ../common/bthome/bthome.cpp
    ../common/bthome/bthome_decode.cpp
    ../common/bthome/aes_ccm.cpp
)

# Display profile pulls in the OLED + buzzer + button code paths.
target_sources_ifdef(CONFIG_CARL_DISPLAY_PROFILE app PRIVATE
    src/ui/oled.cpp
    src/ui/buzzer.cpp
    src/ui/button.cpp
)

# QR rendering on the OLED — relies on Nayuki's qrcodegen-c vendored at
# firmware/common/qr/. See firmware/common/qr/README.md for the curl
# commands; build-side this is opt-in via CONFIG_CARL_HAS_QR=y.
target_sources_ifdef(CONFIG_CARL_HAS_QR app PRIVATE
    ../common/qr/qrcodegen.c
)

target_include_directories(app PRIVATE
    src
    ../common
    ../common/bthome
    ../common/qr
)
