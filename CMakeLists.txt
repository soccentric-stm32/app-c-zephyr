#
# CMakeLists.txt - Zephyr demo application (ZDEMO), NUCLEO-F767ZI
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Date:    2026-11-03
# SRS:     SRS-ZEPHYR-F767 (ZCON-001..003, ZGEN-001..007, ZLNK-001..004)
#
# Freestanding Zephyr application (ZCON-003): built against $ZEPHYR_BASE in
# the stm32-dev-zephyr container, without a west workspace.
#   scripts/build.sh [clean]
#

cmake_minimum_required(VERSION 3.20.0)

set(BOARD nucleo_f767zi)
set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/app.overlay")

# --- Version and build metadata (ZGEN-002) ---------------------------------
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION_LINES)
foreach(_line IN LISTS VERSION_LINES)
    if(_line MATCHES "^VERSION_MAJOR[ \t]*=[ \t]*([0-9]+)")
        set(APP_VERSION_MAJOR "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "^VERSION_MINOR[ \t]*=[ \t]*([0-9]+)")
        set(APP_VERSION_MINOR "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "^PATCHLEVEL[ \t]*=[ \t]*([0-9]+)")
        set(APP_VERSION_PATCH "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(NOT DEFINED APP_VERSION_MAJOR)
    set(APP_VERSION_MAJOR 0)
endif()
if(NOT DEFINED APP_VERSION_MINOR)
    set(APP_VERSION_MINOR 1)
endif()
if(NOT DEFINED APP_VERSION_PATCH)
    set(APP_VERSION_PATCH 0)
endif()
set(APP_VERSION "${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_PATCH}")
set(APP_FIRMWARE_NAME "zephyr-app")

execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short=12 HEAD
    OUTPUT_VARIABLE APP_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE APP_GIT_COMMIT_FULL OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT APP_GIT_COMMIT)
    set(APP_GIT_COMMIT "unknown")
    set(APP_GIT_COMMIT_FULL "")
else()
    execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" status --porcelain
        OUTPUT_VARIABLE APP_GIT_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(APP_GIT_DIRTY)
        string(APPEND APP_GIT_COMMIT "-dirty")
    endif()
endif()
string(TIMESTAMP APP_BUILD_DATE "%Y-%m-%d %H:%M:%S" UTC)
string(TIMESTAMP APP_BUILD_TIMESTAMP "%s" UTC)

# The image's Zephyr tree has no .git (ZCON-002); its version is recorded in
# /opt/zephyrproject/VERSION by docker/Dockerfile.
if(NOT DEFINED BUILD_VERSION AND EXISTS "$ENV{ZEPHYR_BASE}/../VERSION")
    file(STRINGS "$ENV{ZEPHYR_BASE}/../VERSION" BUILD_VERSION LIMIT_COUNT 1)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_app LANGUAGES C)

target_compile_definitions(app PRIVATE
    APP_VERSION_STR="${APP_VERSION}"
    APP_FIRMWARE_NAME="${APP_FIRMWARE_NAME}"
    APP_GIT_COMMIT="${APP_GIT_COMMIT}"
    APP_BUILD_DATE="${APP_BUILD_DATE}"
    APP_BUILD_TIMESTAMP=${APP_BUILD_TIMESTAMP}
    APP_FLATCC_HEAP_SIZE=12288
    FLATCC_ALLOC=app_flatcc_alloc
    FLATCC_FREE=app_flatcc_free
    FLATCC_REALLOC=app_flatcc_realloc
    FLATCC_CALLOC=app_flatcc_calloc
    FLATCC_ALIGNED_ALLOC=app_flatcc_aligned_alloc
    FLATCC_ALIGNED_FREE=app_flatcc_aligned_free
    FLATCC_EMITTER_ALLOC=app_flatcc_alloc
    FLATCC_EMITTER_FREE=app_flatcc_free)

# The generated flatcc headers and runtime see the fixed-arena allocator
# through this force-include (ZRT-004).
target_compile_options(app PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/src/flatcc_port.h")

target_include_directories(app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/flatcc/include")

# The generated flatcc headers use compact macro formatting that trips
# -Wmisleading-indentation in the one translation unit that includes them.
set_source_files_properties(src/comms.c PROPERTIES
    COMPILE_OPTIONS "-Wno-misleading-indentation")

target_sources(app PRIVATE
    src/main.c
    src/fatal.c
    src/iwdg.c
    src/handover.c
    src/reset_cause.c
    src/flash_confirm.c
    src/identity.c
    src/uart_hwlink.c
    src/hwlink.c
    src/flatcc_port.c
    src/led.c
    src/button.c
    src/health.c
    src/comms.c)

# flatcc 0.6.1 runtime as released.  Only builder, verifier and emitter are
# linked; JSON support is not used.
set(FLATCC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/flatcc")
target_sources(app PRIVATE
    "${FLATCC_ROOT}/src/runtime/builder.c"
    "${FLATCC_ROOT}/src/runtime/verifier.c"
    "${FLATCC_ROOT}/src/runtime/emitter.c"
    "${FLATCC_ROOT}/src/runtime/refmap.c")

# --- HWLINK schema (ZLNK-001) ------------------------------------------------
# The generated C headers are committed so the container does not need a host
# flatcc compiler.  When flatcc is available, regenerate them as part of the
# build; the build-tree copy takes precedence over src/generated.
find_program(FLATCC flatcc)
if(FLATCC)
    set(FLATCC_OUT "${CMAKE_BINARY_DIR}/generated")
    add_custom_command(
        OUTPUT
            "${FLATCC_OUT}/hwlink_reader.h"
            "${FLATCC_OUT}/hwlink_builder.h"
            "${FLATCC_OUT}/hwlink_verifier.h"
            "${FLATCC_OUT}/flatbuffers_common_reader.h"
            "${FLATCC_OUT}/flatbuffers_common_builder.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FLATCC_OUT}"
        COMMAND ${FLATCC} -a -o "${FLATCC_OUT}" "${CMAKE_CURRENT_SOURCE_DIR}/protocol/hwlink.fbs"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/protocol/hwlink.fbs"
        COMMENT "Generating HWLINK FlatBuffers C headers")
    add_custom_target(hwlink_schema DEPENDS
        "${FLATCC_OUT}/hwlink_reader.h"
        "${FLATCC_OUT}/hwlink_builder.h"
        "${FLATCC_OUT}/hwlink_verifier.h")
    add_dependencies(app hwlink_schema)
endif()

# --- Build products (ZGEN-001, ZGEN-002) -------------------------------------
find_package(Python3 COMPONENTS Interpreter REQUIRED)
get_filename_component(_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(SIZE_TOOL NAMES arm-zephyr-eabi-size arm-none-eabi-size
    HINTS "${_compiler_dir}" REQUIRED)

add_custom_target(zephyr_app_outputs ALL
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/zephyr/zephyr.elf"
            "${CMAKE_BINARY_DIR}/zephyr-app.elf"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/zephyr/zephyr.bin"
            "${CMAKE_BINARY_DIR}/zephyr-app.bin"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/zephyr/zephyr.hex"
            "${CMAKE_BINARY_DIR}/zephyr-app.hex"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/zephyr/zephyr.map"
            "${CMAKE_BINARY_DIR}/zephyr-app.map"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mkimage.py"
            --input "${CMAKE_BINARY_DIR}/zephyr-app.bin"
            --output "${CMAKE_BINARY_DIR}/zephyr-app.img"
            --version "${APP_VERSION}"
            --commit "${APP_GIT_COMMIT_FULL}"
            --timestamp "${APP_BUILD_TIMESTAMP}"
            --name "${APP_FIRMWARE_NAME}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_footprint.py"
            --size-tool "${SIZE_TOOL}"
            --max-flash 131072
            --max-ram 65536
            "${CMAKE_BINARY_DIR}/zephyr-app.elf"
    DEPENDS zephyr_final
    BYPRODUCTS
        "${CMAKE_BINARY_DIR}/zephyr-app.elf"
        "${CMAKE_BINARY_DIR}/zephyr-app.bin"
        "${CMAKE_BINARY_DIR}/zephyr-app.hex"
        "${CMAKE_BINARY_DIR}/zephyr-app.map"
        "${CMAKE_BINARY_DIR}/zephyr-app.img"
    COMMENT "Copying zephyr-app.* and generating the BOOT image file")
