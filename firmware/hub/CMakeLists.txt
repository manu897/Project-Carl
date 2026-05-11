# Carl Hub — ESP-IDF project.
# Build: idf.py set-target esp32 && idf.py build flash monitor

cmake_minimum_required(VERSION 3.16)

# firmware/common/ holds plain-C++ shared headers (BTHome v2 decoder,
# AES-CCM helpers, sensor drivers). It's NOT an ESP-IDF component dir —
# we pull headers in via INCLUDE_DIRS and add specific .cpp files as SRCS
# in main/CMakeLists.txt as the corresponding subsystems get wired up
# (Phase 3b adds bthome_decode.cpp + aes_ccm.cpp for AES-CCM decrypt).

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(carl_hub)
