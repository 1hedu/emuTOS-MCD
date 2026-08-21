# Build with the Raspberry Pi Pico SDK (vendored at ../../vendor/pico-sdk):
#   ./build.sh                 (or)
#   mkdir build && cd build && cmake .. && make
# Produces pico_printer.uf2 -- drag onto the Pico's BOOTSEL drive.
# Set PICO_SDK_PATH to override the vendored SDK.

cmake_minimum_required(VERSION 3.13)

# Resolve the SDK: an explicit PICO_SDK_PATH (env or CMake var) wins, else fall
# back to the copy vendored in this repo.
if(DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
elseif(NOT DEFINED PICO_SDK_PATH)
    get_filename_component(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../../vendor/pico-sdk" ABSOLUTE)
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_printer C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(pico_printer main.c)

target_link_libraries(pico_printer
    pico_stdlib
    hardware_uart
    hardware_gpio
    hardware_irq
)

pico_add_extra_outputs(pico_printer)   # .uf2 / .hex / .bin / .map
