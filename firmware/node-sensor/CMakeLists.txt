cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(carl_node_sensor)

target_sources(app PRIVATE
    src/main.c
    src/sensors.c
    src/thresholds.c
    src/calibration.c
    src/keystore.c
    src/bthome_emit.c
)

# Display profile: pull in OLED + buzzer + button code paths.
target_sources_ifdef(CONFIG_CARL_DISPLAY_PROFILE app PRIVATE
    src/ui/oled.c
    src/ui/buzzer.c
    src/ui/button.c
)

target_include_directories(app PRIVATE
    src
    ../common
)
