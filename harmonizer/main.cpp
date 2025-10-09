#include "ComputerCard.h"

class SimplePitchShifter : public ComputerCard
{
    // Single delay buffer for pitch shifting
    static const int DELAY_SIZE = 1024; // ~21ms at 48kHz - small and safe
    int16_t delayBuffer[DELAY_SIZE];
    int writeIndex;

    // Harmonic modes
    enum HarmonicMode { THIRD = 0, FIFTH = 1, OCTAVE = 2 };
    HarmonicMode currentMode;
    bool zSwitchPressed;
    bool lastZSwitchState;

    // Pitch shifting parameters
    int pitchOffset;    // Delay offset in samples (controls pitch)
    int dryWetMix;      // 0-4095: 0=dry, 4095=wet

    // LED counter
    int ledCounter;

public:
    SimplePitchShifter() : writeIndex(0), pitchOffset(100), dryWetMix(2048),
                          currentMode(THIRD), zSwitchPressed(false), lastZSwitchState(false), ledCounter(0) {
        // Clear delay buffer
        for (int i = 0; i < DELAY_SIZE; i++) {
            delayBuffer[i] = 0;
        }
    }

private:
    void updateLEDs() {
        ledCounter++;
        if (ledCounter >= 12000) { // Update 4 times per second
            ledCounter = 0;

            // Clear all LEDs
            for (int i = 0; i < 6; i++) {
                LedOff(i);
            }

            // Show current harmonic mode with LEDs 0-2
            if (currentMode == THIRD) LedOn(0);       // Third
            else if (currentMode == FIFTH) LedOn(1);  // Fifth
            else if (currentMode == OCTAVE) LedOn(2); // Octave

            // Show mix level with LED 3
            if (dryWetMix > 2000) LedOn(3);
        }
    }

protected:
    void ProcessSample() override {
        // Get audio input
        int16_t audioIn = AudioIn1();

        // Read controls
        int mainKnob = KnobVal(Main);      // Dry/wet mix
        Switch switchPos = SwitchVal();    // Mode cycling

        // Handle switch for mode cycling (Down position cycles modes)
        bool switchDown = (switchPos == Down);
        if (switchDown && !lastZSwitchState) {
            // Switch pressed to down, cycle to next mode
            currentMode = (HarmonicMode)((currentMode + 1) % 3);
        }
        lastZSwitchState = switchDown;

        // Calculate fixed harmonic pitch offsets (samples at 48kHz)
        // Longer delays = LOWER pitch, shorter delays = HIGHER pitch
        switch (currentMode) {
            case THIRD:  pitchOffset = 80;  break;  // High pitch (short delay)
            case FIFTH:  pitchOffset = 120; break;  // Medium pitch (working value)
            case OCTAVE: pitchOffset = 300; break;  // Low pitch (long delay)
        }

        // Map Main knob to dry/wet mix directly
        dryWetMix = mainKnob;

        // Debug: Show Main knob value ranges with LEDs 4-5
        if (mainKnob < 1024) LedOn(4);        // 0-25%
        else if (mainKnob > 3071) LedOn(5);   // 75-100%

        // Write input to delay buffer
        delayBuffer[writeIndex] = audioIn;

        // Calculate read index with bounds checking
        int readIndex = writeIndex - pitchOffset;
        if (readIndex < 0) readIndex += DELAY_SIZE;

        // Get delayed (pitch-shifted) sample
        int16_t wetSample = delayBuffer[readIndex];

        // Mix dry and wet signals with linear crossfade
        // dryWetMix: 0=100% dry, 4095=100% wet
        int32_t dryGain = 4095 - dryWetMix;
        int32_t wetGain = dryWetMix;
        int32_t output = ((audioIn * dryGain) + (wetSample * wetGain)) / 4095;

        // Clamp output
        if (output > 2047) output = 2047;
        if (output < -2048) output = -2048;

        // Output to both channels
        AudioOut1((int16_t)output);
        AudioOut2((int16_t)output);

        // Advance write pointer
        writeIndex = (writeIndex + 1) % DELAY_SIZE;

        // Update LEDs
        updateLEDs();
    }
};

// Main entry point
int main() {
    SimplePitchShifter card;
    card.Run();
    return 0;
}