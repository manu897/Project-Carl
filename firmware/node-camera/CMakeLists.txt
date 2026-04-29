# XIAO ESP32-S3 Sense camera node — ESP-IDF project.
# Build: idf.py set-target esp32s3 && idf.py build flash monitor

cmake_minimum_required(VERSION 3.16)

# Pull in the shared firmware/common headers so main/ can include them.
set(EXTRA_COMPONENT_DIRS
    "../common"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(carl_node_camera)
