cmake_minimum_required(VERSION 3.13)

# Set board type for Raspberry Pi Pico
set(PICO_BOARD pico)

# Include Pico SDK
include(pico_sdk_import.cmake)

# Project name and languages
project(midi_cc C CXX ASM)

# Set C++ standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Add executable target
add_executable(midi_cc
    main.cpp
)

# Add USB descriptor source
target_sources(midi_cc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/usb_descriptors.c
)

# Link libraries required by the Pico SDK, ComputerCard, and TinyUSB MIDI
target_link_libraries(midi_cc
    pico_stdlib
    hardware_gpio
    hardware_pwm
    hardware_adc
    hardware_dma
    hardware_spi
    hardware_i2c
    hardware_flash
    hardware_irq
    hardware_clocks
    pico_multicore
    pico_unique_id
    tinyusb_device
    tinyusb_board
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(midi_cc)

# Disable stdio USB (TinyUSB MIDI takes over the USB port)
pico_enable_stdio_usb(midi_cc 0)
pico_enable_stdio_uart(midi_cc 0)

# Set compiler flags for optimization
target_compile_options(midi_cc PRIVATE
    -Wall
    -Wextra
    -Wdouble-promotion
    -Wfloat-conversion
    -O2
    -ffast-math
    -funroll-loops
)

# Define preprocessor macros
target_compile_definitions(midi_cc PRIVATE
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

# Include directories
target_include_directories(midi_cc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
