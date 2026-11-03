#
# CMakeLists.txt - Zephyr demo application (ZDEMO), NUCLEO-F767ZI
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Date:    2026-11-03
# SRS:     SRS-ZEPHYR-F767 (ZCON-001..003, ZGEN-001)
#
# Freestanding Zephyr application (ZCON-003): built against $ZEPHYR_BASE in
# the stm32-dev-zephyr container, without a west workspace.
#   scripts/build.sh [clean]
#

cmake_minimum_required(VERSION 3.20.0)

set(BOARD nucleo_f767zi)

# The image's Zephyr tree has no .git (ZCON-002); its version is recorded in
# /opt/zephyrproject/VERSION by docker/Dockerfile.
if(NOT DEFINED BUILD_VERSION AND EXISTS "$ENV{ZEPHYR_BASE}/../VERSION")
    file(STRINGS "$ENV{ZEPHYR_BASE}/../VERSION" BUILD_VERSION LIMIT_COUNT 1)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_app LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/fatal.c)
