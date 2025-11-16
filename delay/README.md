# Audio Delay for Music Thing Modular Workshop System

An audio delay effect for the Music Thing Modular Workshop System Computer Card.

## Features

- Audio delay with variable delay time (2ms to 2.0 seconds)
- Feedback control with infinite sustain capability
- Dry/wet mix
- CV modulation inputs for delay time and feedback
- **Tap tempo** - tap rhythm to set delay time musically
- **Freeze/hold** - gate input to freeze buffer and sustain repeats
- Four delay modes: CLEAN, SATURATION, SHIMMER, LOFI
- Stereo output with mode-dependent width
- DC offset filtering to prevent buildup

## Hardware Interface

### Inputs
- **Audio In 1**: Main audio input
- **Audio In 2**: Secondary audio input
- **CV In 1**: Delay time modulation
- **CV In 2**: Feedback amount modulation
- **Pulse In 1**: Tap tempo - tap rhythm to set delay time
- **Pulse In 2**: Freeze/hold - gate high freezes buffer, repeats sustain

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

## Pulse Input Features

### Tap Tempo (Pulse In 1)
Send rhythmic triggers to set delay time musically:
- Tap a rhythm on Pulse In 1 to set delay time to match
- Accepts tap intervals from 50ms to 3 seconds
- Overrides X knob and CV1 when active
- Returns to manual control after 5 seconds of no taps
- Perfect for syncing delays to your music's tempo

### Freeze/Hold (Pulse In 2)
Freeze the delay buffer while audio continues to repeat:
- Gate high: Stops recording new audio, existing delays continue to feedback
- Gate low: Normal recording resumes
- Creates infinite sustain pad textures
- Great for ambient and experimental music
- Current repeats evolve through feedback and mode effects

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
- Pitch-shifted reverb effect with dense harmonic stacking
- +7 semitones (perfect fifth) on first delay
- +7 semitones added per feedback iteration (stacking fifths)
- 10% stereo width for dramatic, expansive soundscape
- Aggressive highpass filtering emphasizes upper harmonics
- Creates bright, cascading harmonic staircase effect

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

- **Buffer size**: 96,000 samples (2.0 seconds at 48kHz, 192 KB memory)
- **Delay range**: 100 to 95,000 samples (2ms to 2.0 seconds)
- **Interpolation**: Linear interpolation with 128x fractional precision
- **Smoothing**: Exponential smoothing (α=255/256) prevents zipper noise
- **Hysteresis**: 8-point threshold on delay control prevents ADC jitter (except LOFI mode)
- **DC filtering**: One-pole highpass filter prevents DC offset buildup in feedback path
- **Feedback**: Quadratic crossfade with 5% minimum input gain for infinite sustain

