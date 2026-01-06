cmake_minimum_required(VERSION 3.13)

# Set board type for Raspberry Pi Pico
set(PICO_BOARD pico)

# Include Pico SDK
include(pico_sdk_import.cmake)

# Project name and languages
project(resonating-strings C CXX ASM)

# Set C++ standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Add executable target
add_executable(resonating-strings
    main.cpp
)

# Link libraries required by the Pico SDK and ComputerCard
target_link_libraries(resonating-strings
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
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(resonating-strings)

# Enable USB output for debugging (optional)
pico_enable_stdio_usb(resonating-strings 1)
pico_enable_stdio_uart(resonating-strings 0)

# Set compiler flags for optimization
target_compile_options(resonating-strings PRIVATE
    -Wall
    -Wextra
    -O2
    -ffast-math
    -funroll-loops
)

# Define preprocessor macros
target_compile_definitions(resonating-strings PRIVATE
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
)

# Include directories
target_include_directories(resonating-strings PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
