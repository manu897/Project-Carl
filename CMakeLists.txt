cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(project_carl)

target_sources(app PRIVATE src/main.c)

target_sources_ifdef(CONFIG_BME688 app PRIVATE src/bme688_interface.c)