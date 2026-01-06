# Resonating Strings

A sympathetic resonator workshop card inspired by the Mutable Instruments Rings module. This implements three resonating strings using Karplus-Strong synthesis, creating rich, harmonic textures.

## Description

The sympathetic resonator simulates the behavior of strings that vibrate in response to external excitation. When you feed an audio signal into the module, it excites three virtual strings tuned to different harmonic relationships. This creates a shimmering, resonant effect similar to the sound of sympathetic strings on instruments like the sitar or tanpura.

## How It Works

The module uses Karplus-Strong synthesis, a simple but effective physical modeling technique:
- Each string is implemented as a delay line with feedback
- A lowpass filter in the feedback path simulates damping (energy loss)
- The delay time determines the pitch of each string
- Input signals excite the strings, which then resonate at their tuned frequencies

## Controls

### Knobs
- **X Knob**: Base frequency (fundamental pitch) of the resonator
- **Y Knob**: Damping amount (higher = more resonance/longer decay)
- **Main Knob**: Dry/wet mix (0 = dry signal, full = wet resonator output)

### CV Inputs
- **CV1**: Frequency modulation (adds to X knob)
- **CV2**: Reserved for future brightness control

### Switch
Cycles through four tuning modes:
- **Mode 1 (HARMONIC)**: Harmonic series - 1:1, 2:1, 3:1 (fundamental, octave, octave+fifth)
- **Mode 2 (FIFTH)**: Musical intervals - 1:1, 3:2, 2:1 (fundamental, perfect fifth, octave)
- **Mode 3 (MAJOR)**: Major triad - 1:1, 5:4, 3:2 (root, major third, perfect fifth)
- **Mode 4 (MINOR)**: Minor triad - 1:1, 6:5, 3:2 (root, minor third, perfect fifth)

### Pulse Inputs
- **Pulse In 1**: Reserved for future use (trigger string excitation)
- **Pulse In 2**: Reserved for future use (freeze/hold)

## LED Indicators

- **LED 0**: HARMONIC mode active
- **LED 1**: FIFTH mode active
- **LED 2**: MAJOR mode active
- **LED 3**: MINOR mode active
- **LED 4**: Input level indicator
- **LED 5**: High resonance indicator

## Sound Design Tips

1. **Subtle Shimmer**: Set the mix knob around 20-30%, use FIFTH mode, moderate damping
2. **Drone Pad**: Full wet mix, low damping (high Y knob), HARMONIC mode
3. **Plucked Resonance**: Feed in percussive sounds, moderate damping, MAJOR or MINOR modes
4. **Pitch Tracking**: Use steady tones and adjust X knob to match the input frequency
5. **Modulation**: Patch an LFO into CV1 for vibrato/tremolo effects

## Future Enhancements

Potential additions for this module:
- Individual string level controls
- Brightness/tone control via CV2
- Trigger input to manually excite strings
- Stereo output with string panning
- More complex damping models
- Position parameter (excitation point on string)

## Technical Details

- Sample Rate: 48kHz
- Maximum delay length: 960 samples (~50Hz minimum frequency)
- Minimum delay length: 60 samples (~800Hz maximum frequency)
- Number of strings: 3
- Synthesis method: Karplus-Strong with one-pole lowpass damping

## Building

Use the standard Workshop System build process:
```bash
./build.sh
```

This will generate a `resonating-strings.uf2` file in the `build/` directory.

## Inspiration

This module is inspired by the sympathetic resonator mode of the Mutable Instruments Rings module, which uses a bank of parallel resonators to create rich, harmonic textures. The Rings module features more sophisticated DSP with multiple resonator modes, but this simplified implementation captures the essential character of sympathetic resonance.

## References

- [Mutable Instruments Rings Source Code](https://github.com/pichenettes/eurorack/tree/master/rings)
- [Karplus-Strong Synthesis](https://en.wikipedia.org/wiki/Karplus%E2%80%93Strong_string_synthesis)
