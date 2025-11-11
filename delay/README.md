# Audio Delay for Music Thing Modular Workshop System

An audio delay effect for the Music Thing Modular Workshop System Computer Card.

## Features

- Audio delay with variable delay time
- Feedback control
- Dry/wet mix
- CV modulation inputs

## Hardware Interface

### Inputs
- **Audio In 1**: Main audio input
- **Audio In 2**: Secondary audio input
- **CV In 1**: Modulation input (can control delay time)
- **CV In 2**: Modulation input (can control feedback)
- **Pulse In 1**: Trigger/gate input
- **Pulse In 2**: Trigger/gate input

### Outputs
- **Audio Out 1**: Processed audio output
- **Audio Out 2**: Processed audio output (duplicate)
- **CV Out 1**: Control voltage output
- **CV Out 2**: Control voltage output
- **Pulse Out 1**: Pulse output
- **Pulse Out 2**: Pulse output

### Controls
- **Main Knob**: Dry/wet mix
- **X Knob**: Delay time
- **Y Knob**: Feedback amount
- **Switch**: Mode selection (Up/Middle/Down)

### LEDs
- 6 LEDs available for status indication

## Building

1. Install required tools:
   - CMake
   - ARM GCC toolchain (`arm-none-eabi-gcc`)
   - Make

2. Set up Pico SDK path (optional):
   ```bash
   export PICO_SDK_PATH=/path/to/pico-sdk
   ```

3. Build the project:
   ```bash
   chmod +x build.sh
   ./build.sh
   ```

## Flashing

1. Hold down the BOOTSEL button on the Computer Card
2. Connect USB cable
3. Release BOOTSEL button
4. Copy `build/delay.uf2` to the RPI-RP2 drive that appears

## Development

The main code is in `main.cpp`. The `AudioDelay` class inherits from `ComputerCard` and implements the `ProcessSample()` method, which is called at 48kHz audio rate.

## TODO

- [ ] Implement delay time calculation with CV modulation
- [ ] Add interpolated delay line reading
- [ ] Implement feedback path
- [ ] Add dry/wet mixing
- [ ] Implement LED feedback indicators
- [ ] Add different delay modes via switch
- [ ] Add optional effects (filtering, modulation, etc.)
