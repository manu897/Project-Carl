cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(carl_node_room LANGUAGES C CXX)

# Per-room environmental sensor node, built around the Nordic Thingy:53.
# Shares the BTHome v2 codec + keystore pattern with the plant-probe node
# (firmware/node-sensor/) so the hub side decodes both with the same path.
# Different sensors_cpp because the onboard chips are different (BME688 +
# BH1749 here vs. BME280 + VEML7700 on the XIAO).
target_sources(app PRIVATE
    src/main.cpp
    src/sensors.cpp
    src/keystore.cpp
    src/bthome_emit.cpp
    src/ui/led.cpp
    src/ui/buzzer.cpp
    # Shared BTHome codec — compiled into the app build, identical to how
    # node-sensor pulls it in.
    ../common/bthome/bthome.cpp
    ../common/bthome/bthome_decode.cpp
    ../common/bthome/aes_ccm.cpp
)

target_include_directories(app PRIVATE
    src
    ../common
    ../common/bthome
)
