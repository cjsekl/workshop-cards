# Workshop System Harmonizer - Build Instructions

This document explains how to build the DSP harmonizer firmware for the Music Thing Modular Workshop System.

## Prerequisites

### Required Software

1. **CMake** (version 3.13 or later)
2. **ARM GCC Toolchain** (arm-none-eabi-gcc)
3. **Git** (for SDK download if needed)
4. **Make** or **Ninja** build system

### Installation on Ubuntu/Debian

```bash
sudo apt update
sudo apt install cmake gcc-arm-none-eabi build-essential git
```

### Installation on macOS

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install required tools
brew install cmake
brew install --cask gcc-arm-embedded
```

### Installation on Windows

1. Download and install CMake from https://cmake.org/download/
2. Install ARM GCC toolchain from https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm
3. Install Git from https://git-scm.com/download/win

## Raspberry Pi Pico SDK

The build system will automatically download the Pico SDK if `PICO_SDK_PATH` is not set. 

### Manual SDK Installation (Optional)

```bash
# Clone the Pico SDK
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init

# Set environment variable
export PICO_SDK_PATH=/path/to/pico-sdk
```

Add the export line to your `.bashrc` or `.zshrc` for permanent use.

## Building the Firmware

### Quick Build (Recommended)

```bash
# Navigate to the project directory
cd /path/to/workshop_system/harmonizer

# Run the build script
./build.sh
```

### Manual Build

```bash
# Create build directory
mkdir build && cd build

# Configure the project
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build the firmware
make -j$(nproc)
```

## Build Output

After a successful build, you'll find these files in the `build/` directory:

- **`harmonizer.uf2`** - Main flash file for Workshop System (this is what you need!)
- `harmonizer.elf` - Executable with debug symbols
- `harmonizer.bin` - Raw binary file
- `harmonizer.hex` - Intel HEX format file
- `harmonizer.map` - Memory map file

## Flashing to Workshop System

1. **Enter Bootloader Mode:**
   - Hold down the **BOOTSEL** button on the Workshop System computer module
   - Connect USB cable to your computer
   - Release the **BOOTSEL** button
   - The module should appear as a USB drive named "RPI-RP2"

2. **Flash the Firmware:**
   - Copy `harmonizer.uf2` to the RPI-RP2 drive
   - The module will automatically reboot and run the new firmware
   - The USB drive will disappear when flashing is complete

3. **Verify Installation:**
   - The harmonizer should start immediately
   - LEDs should indicate the current mode
   - Check that audio processing is working

## Build Configuration

### Build Types

- **Release** (default): Optimized for performance (-O2)
- **Debug**: Includes debug symbols and minimal optimization

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

### Compiler Flags

The build system uses these optimization flags for Release builds:
- `-O2` - Level 2 optimization
- `-ffast-math` - Fast floating-point math
- `-funroll-loops` - Loop unrolling for performance

### USB Serial Output

By default, USB serial output is enabled for debugging. To disable:

```bash
cmake .. -DPICO_STDIO_USB=0
```

## Troubleshooting

### SDK Not Found

If you get "SDK not found" errors:

```bash
# Set the SDK path explicitly
export PICO_SDK_PATH=/path/to/pico-sdk
```

Or let the build system download it automatically by unsetting the variable:

```bash
unset PICO_SDK_PATH
```

### Build Errors

1. **Missing toolchain:**
   ```
   arm-none-eabi-gcc: command not found
   ```
   Install the ARM GCC toolchain (see Prerequisites section)

2. **CMake version too old:**
   ```
   CMake 3.13 or higher is required
   ```
   Update CMake to a newer version

3. **Out of memory during build:**
   ```
   virtual memory exhausted
   ```
   Reduce parallel jobs: `make -j2` instead of `make -j$(nproc)`

### Clean Build

To start fresh:

```bash
rm -rf build/
./build.sh
```

## Development Workflow

### Iterative Development

For faster rebuilds during development:

```bash
cd build/
make harmonizer
```

This only rebuilds changed files.

### Debug Build

For debugging with GDB:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
make harmonizer
```

## File Structure

```
harmonizer/
├── main.cpp              # Main harmonizer implementation  
├── ComputerCard.h         # Workshop System hardware library
├── CMakeLists.txt         # Build configuration
├── pico_sdk_import.cmake  # Pico SDK import script
├── build.sh              # Build script
├── BUILD.md              # This file
└── build/                # Build output directory
    ├── harmonizer.uf2    # Flash file (created after build)
    └── ...               # Other build artifacts
```

## Performance Notes

- The harmonizer runs at 48kHz sample rate
- Control processing is limited to 100Hz for efficiency
- Build uses `-ffast-math` for optimal DSP performance
- Memory usage optimized for RP2040's 264KB RAM

## Support

For build issues:
1. Check that all prerequisites are installed
2. Verify Pico SDK is properly set up
3. Try a clean build
4. Check the Music Thing Modular Workshop System documentation

The generated `harmonizer.uf2` file contains the complete firmware ready for installation on your Workshop System.