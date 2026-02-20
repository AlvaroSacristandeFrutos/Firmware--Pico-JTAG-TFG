cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH $ENV{HOME}/.pico-sdk/sdk/2.2.0)
include(pico_sdk_import.cmake)

project(jlink_pico_probe C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(jlink_pico_probe
    src/main.c
    src/board/clock_init.c
    src/board/gpio_init.c
    src/usb/usb_device.c
    src/usb/usb_descriptors.c
    src/jtag/jtag_pio.c
    src/jtag/jtag_tap.c
    src/jlink/jlink_handler.c
    src/jlink/jlink_caps.c
    src/cdc/cdc_uart.c
    src/util/led.c
    src/util/adc.c
)

pico_generate_pio_header(jlink_pico_probe
    ${CMAKE_CURRENT_LIST_DIR}/src/jtag/jtag.pio
)

target_include_directories(jlink_pico_probe PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/board
    ${CMAKE_CURRENT_LIST_DIR}/src/usb
    ${CMAKE_CURRENT_LIST_DIR}/src/jtag
    ${CMAKE_CURRENT_LIST_DIR}/src/jlink
    ${CMAKE_CURRENT_LIST_DIR}/src/cdc
    ${CMAKE_CURRENT_LIST_DIR}/src/util
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(jlink_pico_probe
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_regs
    hardware_structs
    hardware_irq
    hardware_resets
    pico_multicore
    pico_unique_id
)

pico_enable_stdio_usb(jlink_pico_probe 0)
pico_enable_stdio_uart(jlink_pico_probe 0)

pico_add_uf2_output(jlink_pico_probe)
