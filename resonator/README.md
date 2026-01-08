# Resonating Strings

A sympathetic resonator workshop card inspired by the Mutable Instruments Rings module and the tanpura.
It has four resonating Karplus-Strong strings that, when excited, creates creating rich, harmonic textures.

## Description

The sympathetic resonator simulates the behavior of strings that vibrate in response to external excitation. When you feed an audio signal into the module, it excites four virtual strings tuned to different harmonic relationships. This creates a shimmering, resonant effect similar to the sound of sympathetic strings on instruments like the sitar or tanpura.

## How It Works

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
- **CV2**: Damping modulation (adds to Y knob)

### Switch
Cycles through seven chord modes (press switch down to advance):
- **HARMONIC**: Harmonic series - 1:1, 2:1, 3:1, 4:1
- **FIFTH**: Stacked fifths - 1:1, 3:2, 2:1, 3:1
- **MAJOR7**: Major 7th chord - 1:1, 5:4, 3:2, 15:8
- **MINOR7**: Minor 7th chord - 1:1, 6:5, 3:2, 9:5
- **DIM**: Diminished - 1:1, 6:5, 36:25, 3:2
- **SUS4**: Suspended 4th - 1:1, 4:3, 3:2, 2:1
- **ADD9**: Major add 9 - 1:1, 5:4, 3:2, 9:4

### Pulse Inputs
- **Pulse In 1**: Trigger string excitation (pluck with noise burst)
- **Pulse In 2**: Reserved for future use

## LED Indicators

All 6 LEDs indicate the current chord mode:
- **LED 0**: HARMONIC mode (also lit for ADD9)
- **LED 1**: FIFTH mode
- **LED 2**: MAJOR7 mode
- **LED 3**: MINOR7 mode
- **LED 4**: DIM mode
- **LED 5**: SUS4 mode (also lit for ADD9)

## Future Enhancements

Potential additions for this module:
- More chord modes?
- 1V/octave pitch tracking on CV1

## Building

Use the standard Workshop System build process:
```bash
./build.sh
```

This will generate a `resonator.uf2` file in the `build/` directory.

## Inspiration

This module is inspired by the sympathetic resonator mode of the Mutable Instruments Rings module, which uses a bank of parallel resonators to create rich, harmonic textures. The Rings module features more sophisticated DSP with multiple resonator modes, but this simplified implementation captures the essential character of sympathetic resonance.

## References

- [Mutable Instruments Rings Source Code](https://github.com/pichenettes/eurorack/tree/master/rings)
- [Tanpura](https://en.wikipedia.org/wiki/Tanpura)
- [Karplus-Strong Synthesis](https://en.wikipedia.org/wiki/Karplus%E2%80%93Strong_string_synthesis)
