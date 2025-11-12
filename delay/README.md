# Audio Delay for Music Thing Modular Workshop System

An audio delay effect for the Music Thing Modular Workshop System Computer Card.

## Features

- Audio delay with variable delay time (2ms to 1.5 seconds)
- Feedback control with infinite sustain capability
- Dry/wet mix
- CV modulation inputs for delay time and feedback
- Four delay modes: CLEAN, SATURATION, SHIMMER, LOFI
- Stereo output with mode-dependent width
- DC offset filtering to prevent buildup

## Hardware Interface

### Inputs
- **Audio In 1**: Main audio input
- **Audio In 2**: Secondary audio input
- **CV In 1**: Delay time
- **CV In 2**: Feedback amount
- **Pulse In 1**: Trigger/gate input (UNUSED)
- **Pulse In 2**: Trigger/gate input (UNUSED)

### Outputs
- **Audio Out 1**: Processed audio output (LEFT)
- **Audio Out 2**: Processed audio output (RIGHT)
- **CV Out 1**: Control voltage output (UNUSED)
- **CV Out 2**: Control voltage output (UNUSED)
- **Pulse Out 1**: Pulse output (UNUSED)
- **Pulse Out 2**: Pulse output (UNUSED)

### Controls
- **Main Knob**: Dry/wet mix
- **X Knob**: Delay time
- **Y Knob**: Feedback amount
- **Switch down**: Mode selection

### LEDs
- **LED 0**: Delay time indicator (flashing - faster pulse = longer delay)
- **LED 1**: Feedback indicator (on when feedback > 50%)
- **LED 2**: CLEAN mode indicator
- **LED 3**: SATURATION mode indicator
- **LED 4**: SHIMMER mode indicator
- **LED 5**: LOFI mode indicator

## Delay Modes

Press the switch down to cycle through four delay modes:

### CLEAN Mode (LED 2)
- Clean, transparent digital delay
- Mono output (no stereo spread)
- Hysteresis on delay time control for stable pitch
- Default mode on startup

### SATURATION Mode (LED 3)
- Warm tape-style saturation effect
- Progressive soft clipping increases with each feedback iteration
- 1% stereo width for subtle spatial effect
- ~50% more drive than original implementation

### SHIMMER Mode (LED 4)
- Pitch-shifted reverb effect
- +7 semitones (perfect fifth) on first delay
- +12 semitones (octave) added per feedback iteration
- 3% stereo width for expansive soundscape
- Creates cascading harmony effect

### LOFI Mode (LED 5)
- Clean delay with pitch warble character
- No hysteresis on delay time control - allows ADC noise and micro-movements to modulate pitch
- Creates tape-style wow/flutter and pitch wobble effects
- Mono output (no stereo spread)

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

### Key Implementation Details

- **Buffer size**: 96,000 samples (~2 seconds at 48kHz)
- **Delay range**: 100 to 71,000 samples (2ms to 1.5 seconds)
- **Interpolation**: Linear interpolation with 128x fractional precision
- **Smoothing**: Exponential smoothing (α=255/256) prevents zipper noise
- **Hysteresis**: 8-point threshold on delay control prevents ADC jitter (except LOFI mode)
- **DC filtering**: One-pole highpass filter prevents DC offset buildup in feedback path
- **Feedback**: Quadratic crossfade with 5% minimum input gain for infinite sustain

