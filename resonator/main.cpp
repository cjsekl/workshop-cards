#include "ComputerCard.h"

// Resonator Workshop System Computer Card
// Four resonating strings using Karplus-Strong synthesis
class ResonatingStrings : public ComputerCard
{
private:
    static const int MAX_DELAY_SIZE = 1920;

    // Four string delay lines
    int16_t delayLine1[MAX_DELAY_SIZE];
    int16_t delayLine2[MAX_DELAY_SIZE];
    int16_t delayLine3[MAX_DELAY_SIZE];
    int16_t delayLine4[MAX_DELAY_SIZE];

    int writeIndex1;
    int writeIndex2;
    int writeIndex3;
    int writeIndex4;

    int delayLength1;
    int delayLength2;
    int delayLength3;
    int delayLength4;

    // Filter states for damping
    int32_t filterState1;
    int32_t filterState2;
    int32_t filterState3;
    int32_t filterState4;

    // Chord mode
    enum ChordMode {
        HARMONIC = 0,    // 1:1, 2:1, 3:1, 4:1 (harmonic series)
        FIFTH = 1,       // 1:1, 3:2, 2:1, 3:1 (stacked fifths)
        MAJOR7 = 2,      // 1:1, 5:4, 3:2, 15:8 (major 7th chord)
        MINOR7 = 3,      // 1:1, 6:5, 3:2, 9:5 (minor 7th chord)
        DIM = 4,         // 1:1, 6:5, 36:25, 3:2 (diminished)
        SUS4 = 5,        // 1:1, 4:3, 3:2, 2:1 (suspended 4th)
        ADD9 = 6         // 1:1, 5:4, 3:2, 9:4 (major add 9)
    };
    static const int NUM_MODES = 7;
    ChordMode currentMode;
    bool lastSwitchDown;

    // Excitation detector (for sympathetic response)
    int32_t envelopeFollower;

    // Pulse excitation state
    int32_t pulseExciteEnvelope;
    uint32_t noiseState;

    // DC blocker states for each string
    int32_t dcState1, dcState2, dcState3, dcState4;

    // Initialize delay lines with noise burst
    void initializeString(int16_t* delayLine, int length) {
        for (int i = 0; i < length && i < MAX_DELAY_SIZE; i++) {
            // Simple pseudo-random noise
            int16_t noise = (int16_t)((i * 1103515245 + 12345) & 0x7FFF) - 16384;
            delayLine[i] = noise >> 3;  // Reduce amplitude
        }
    }

    // One-pole lowpass filter for damping
    int32_t dampingFilter(int32_t input, int32_t& state, int32_t coefficient) {
        // state += ((input - state) * coefficient) >> 16
        state += (((input - state) * coefficient + 32768) >> 16);
        return state;
    }

    // Process one string
    int32_t processString(int16_t* delayLine, int& writeIndex, int delayLength,
                         int32_t& filterState, int32_t& dcState, int32_t excitation,
                         int32_t dampingCoeff, int32_t brightness) {
        // Read from delay line
        int readIndex = writeIndex - delayLength;
        if (readIndex < 0) readIndex += MAX_DELAY_SIZE;

        int32_t delayedSample = delayLine[readIndex];

        // Apply damping filter
        int32_t dampedSample = dampingFilter(delayedSample, filterState, dampingCoeff);

        // DC blocker: remove DC offset to prevent accumulation
        // This keeps the resonance "fresh" like at startup
        dcState += (dampedSample - dcState) >> 8;  // Slow DC tracking
        dampedSample -= dcState;  // Subtract DC component

        // Add excitation (input signal)
        int32_t newSample = dampedSample + excitation;

        // Soft clipping to prevent overflow
        if (newSample > 2047) newSample = 2047;
        if (newSample < -2047) newSample = -2047;

        // Write back to delay line
        delayLine[writeIndex] = (int16_t)newSample;

        // Advance write index
        writeIndex = (writeIndex + 1) % MAX_DELAY_SIZE;

        return delayedSample;
    }

    // Calculate frequency ratio based on chord mode and string number
    // Using fixed-point math to avoid floating-point on Cortex-M0+
    // Returns numerator and denominator for each ratio
    void getFrequencyRatios(int& num1, int& den1, int& num2, int& den2,
                            int& num3, int& den3, int& num4, int& den4) {
        // String 1: Fundamental
        num1 = 1;
        den1 = 1;

        switch (currentMode) {
            case HARMONIC:
                // Harmonic series: 1:1, 2:1, 3:1, 4:1
                num2 = 2; den2 = 1;
                num3 = 3; den3 = 1;
                num4 = 4; den4 = 1;
                break;
            case FIFTH:
                // Stacked fifths: 1:1, 3:2, 2:1, 3:1
                num2 = 3; den2 = 2;
                num3 = 2; den3 = 1;
                num4 = 3; den4 = 1;
                break;
            case MAJOR7:
                // Major 7th: 1:1, 5:4, 3:2, 15:8
                num2 = 5; den2 = 4;
                num3 = 3; den3 = 2;
                num4 = 15; den4 = 8;
                break;
            case MINOR7:
                // Minor 7th: 1:1, 6:5, 3:2, 9:5
                num2 = 6; den2 = 5;
                num3 = 3; den3 = 2;
                num4 = 9; den4 = 5;
                break;
            case DIM:
                // Diminished: 1:1, 6:5, 36:25, 3:2
                num2 = 6; den2 = 5;
                num3 = 36; den3 = 25;
                num4 = 3; den4 = 2;
                break;
            case SUS4:
                // Suspended 4th: 1:1, 4:3, 3:2, 2:1
                num2 = 4; den2 = 3;
                num3 = 3; den3 = 2;
                num4 = 2; den4 = 1;
                break;
            case ADD9:
                // Major add 9: 1:1, 5:4, 3:2, 9:4
                num2 = 5; den2 = 4;
                num3 = 3; den3 = 2;
                num4 = 9; den4 = 4;
                break;
        }
    }

public:
    ResonatingStrings() : writeIndex1(0), writeIndex2(0), writeIndex3(0), writeIndex4(0),
                          delayLength1(100), delayLength2(150), delayLength3(200), delayLength4(400),
                          filterState1(0), filterState2(0), filterState3(0), filterState4(0),
                          currentMode(FIFTH), lastSwitchDown(true),
                          envelopeFollower(0),
                          pulseExciteEnvelope(0), noiseState(12345),
                          dcState1(0), dcState2(0), dcState3(0), dcState4(0) {
        // Initialize delay lines with silence
        for (int i = 0; i < MAX_DELAY_SIZE; i++) {
            delayLine1[i] = 0;
            delayLine2[i] = 0;
            delayLine3[i] = 0;
            delayLine4[i] = 0;
        }
    }

protected:
    void ProcessSample() override {
        // Read inputs
        int16_t audioIn1 = AudioIn1();
        int16_t audioIn2 = AudioIn2();
        int32_t audioIn = ((int32_t)audioIn1 + (int32_t)audioIn2 + 1) >> 1;

        // Mode switching
        Switch switchPos = SwitchVal();
        bool switchDown = (switchPos == Down);
        if (switchDown && !lastSwitchDown) {
            currentMode = (ChordMode)((currentMode + 1) % NUM_MODES);
        }
        lastSwitchDown = switchDown;

        // FREQUENCY CONTROL (X Knob + CV1)
        int32_t frequencyKnob = KnobVal(X);  // 0-4095
        int16_t cv1 = CVIn1();

        int32_t combinedFreq = frequencyKnob + cv1;
        if (combinedFreq > 4095) combinedFreq = 4095;
        if (combinedFreq < 0) combinedFreq = 0;

        // Map to delay length (50Hz to 800Hz range)
        // At 48kHz: 50Hz = 960 samples, 800Hz = 60 samples
        const int MIN_DELAY = 60;
        const int MAX_DELAY = 960;
        int32_t baseDelay = MAX_DELAY - ((combinedFreq * (MAX_DELAY - MIN_DELAY)) / 4095);

        // Get frequency ratios based on current chord mode
        // Using integer numerator/denominator to avoid floating-point
        int num1 = 1, den1 = 1, num2 = 2, den2 = 1, num3 = 3, den3 = 1, num4 = 4, den4 = 1;
        getFrequencyRatios(num1, den1, num2, den2, num3, den3, num4, den4);

        // Calculate delay lengths for each string using integer math
        // delay = baseDelay * denominator / numerator
        delayLength1 = (baseDelay * den1) / num1;
        delayLength2 = (baseDelay * den2) / num2;
        delayLength3 = (baseDelay * den3) / num3;
        delayLength4 = (baseDelay * den4) / num4;

        // Clamp to valid range
        if (delayLength1 < 10) delayLength1 = 10;
        if (delayLength2 < 10) delayLength2 = 10;
        if (delayLength3 < 10) delayLength3 = 10;
        if (delayLength4 < 10) delayLength4 = 10;
        if (delayLength1 > MAX_DELAY_SIZE - 1) delayLength1 = MAX_DELAY_SIZE - 1;
        if (delayLength2 > MAX_DELAY_SIZE - 1) delayLength2 = MAX_DELAY_SIZE - 1;
        if (delayLength3 > MAX_DELAY_SIZE - 1) delayLength3 = MAX_DELAY_SIZE - 1;
        if (delayLength4 > MAX_DELAY_SIZE - 1) delayLength4 = MAX_DELAY_SIZE - 1;

        // DAMPING CONTROL (Y Knob + CV2)
        int32_t dampingKnob = KnobVal(Y) + CVIn2();  // 0-4095 knob + CV
        if (dampingKnob > 4095) dampingKnob = 4095;
        if (dampingKnob < 0) dampingKnob = 0;

        // Map to filter coefficient (more damping = lower coefficient = darker sound)
        // Range from 65300 (extremely long decay) to 32000 (moderate decay)
        int32_t dampingCoeff = 32000 + ((dampingKnob * 33300) / 4095);

        // Envelope follower - detect input energy
        int32_t absInput = (audioIn < 0) ? -audioIn : audioIn;
        envelopeFollower = ((envelopeFollower * 255) >> 8) + (absInput >> 3);

        // Excitation amounts for each string based on envelope
        // String 1 gets full input, others get scaled versions (sympathetic response)
        int32_t excitation1 = audioIn >> 2;  // Direct excitation
        int32_t excitation2 = audioIn >> 4;  // Sympathetic response
        int32_t excitation3 = audioIn >> 4;  // Sympathetic response
        int32_t excitation4 = audioIn >> 3;  // 4th string

        // Pulse1 triggers a noise burst to excite strings (like plucking)
        if (PulseIn1RisingEdge()) {
            pulseExciteEnvelope = 2048;  // Start excitation envelope
        }

        // Apply decaying noise burst while envelope is active
        if (pulseExciteEnvelope > 10) {
            noiseState = noiseState * 1103515245 + 12345;
            int32_t noise = (int32_t)((noiseState >> 16) & 0xFFF) - 2048;
            int32_t scaledNoise = (noise * pulseExciteEnvelope) >> 11;
            excitation1 += scaledNoise;
            excitation2 += scaledNoise >> 1;
            excitation3 += scaledNoise >> 1;
            excitation4 += scaledNoise >> 1;
            // Fast decay for short pluck burst
            pulseExciteEnvelope = (pulseExciteEnvelope * 250) >> 8;
        }

        // Process each string
        int32_t out1 = processString(delayLine1, writeIndex1, delayLength1,
                                     filterState1, dcState1, excitation1, dampingCoeff, 0);
        int32_t out2 = processString(delayLine2, writeIndex2, delayLength2,
                                     filterState2, dcState2, excitation2, dampingCoeff, 0);
        int32_t out3 = processString(delayLine3, writeIndex3, delayLength3,
                                     filterState3, dcState3, excitation3, dampingCoeff, 0);
        int32_t out4 = processString(delayLine4, writeIndex4, delayLength4,
                                     filterState4, dcState4, excitation4, dampingCoeff, 0);

        // Mix strings together
        int32_t resonatorOut = (out1 + out2 + out3 + out4) / 4;

        // WET/DRY MIX (Main Knob)
        int32_t mixKnob = KnobVal(Main);  // 0-4095

        int32_t dryGain = 4095 - mixKnob;
        int32_t wetGain = mixKnob;

        int32_t mixedOutput = ((audioIn * dryGain) + (resonatorOut * wetGain) + 2048) >> 12;

        // Clipping
        if (mixedOutput > 2047) mixedOutput = 2047;
        if (mixedOutput < -2047) mixedOutput = -2047;

        int16_t output = (int16_t)mixedOutput;

        // Output to both channels
        AudioOut1(output);
        AudioOut2(output);

        // LED indicators - all 6 LEDs show chord mode
        // LED 0: HARMONIC, LED 1: FIFTH, LED 2: MAJOR7
        // LED 3: MINOR7, LED 4: DIM, LED 5: SUS4
        // ADD9 (mode 6): LEDs 0+5 both on
        LedOn(0, currentMode == HARMONIC || currentMode == ADD9);
        LedOn(1, currentMode == FIFTH);
        LedOn(2, currentMode == MAJOR7);
        LedOn(3, currentMode == MINOR7);
        LedOn(4, currentMode == DIM);
        LedOn(5, currentMode == SUS4 || currentMode == ADD9);
    }
};

int main() {
    static ResonatingStrings resonator;
    resonator.Run();
    return 0;
}
