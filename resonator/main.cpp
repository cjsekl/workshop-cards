#include "ComputerCard.h"

/**
Resonator Workshop System Computer Card - by Johan Eklund
version 0.3 - 2026-01-09

Four resonating strings using Karplus-Strong synthesis
*/

// Delay lookup table for 1V/oct pitch control
// 341 entries per octave, inverse exponential curve
// Higher input = shorter delay = higher pitch
// Use with ExpDelay(): oct = in/341, suboct = in%341, return delay_vals[suboct] >> oct
static const uint16_t delay_vals[341] = {
    61440, 61315, 61190, 61066, 60942, 60818, 60695, 60571, 60448, 60326,
    60203, 60081, 59959, 59837, 59716, 59594, 59473, 59353, 59232, 59112,
    58992, 58872, 58752, 58633, 58514, 58395, 58277, 58158, 58040, 57922,
    57805, 57687, 57570, 57453, 57337, 57220, 57104, 56988, 56872, 56757,
    56642, 56527, 56412, 56297, 56183, 56069, 55955, 55841, 55728, 55615,
    55502, 55389, 55277, 55164, 55052, 54941, 54829, 54718, 54607, 54496,
    54385, 54275, 54164, 54054, 53945, 53835, 53726, 53617, 53508, 53399,
    53291, 53183, 53075, 52967, 52859, 52752, 52645, 52538, 52431, 52325,
    52218, 52112, 52007, 51901, 51796, 51690, 51585, 51481, 51376, 51272,
    51168, 51064, 50960, 50857, 50753, 50650, 50547, 50445, 50342, 50240,
    50138, 50036, 49935, 49833, 49732, 49631, 49530, 49430, 49329, 49229,
    49129, 49030, 48930, 48831, 48731, 48632, 48534, 48435, 48337, 48239,
    48141, 48043, 47945, 47848, 47751, 47654, 47557, 47461, 47364, 47268,
    47172, 47076, 46981, 46885, 46790, 46695, 46600, 46506, 46411, 46317,
    46223, 46129, 46035, 45942, 45849, 45755, 45663, 45570, 45477, 45385,
    45293, 45201, 45109, 45017, 44926, 44835, 44744, 44653, 44562, 44472,
    44381, 44291, 44201, 44112, 44022, 43933, 43843, 43754, 43665, 43577,
    43488, 43400, 43312, 43224, 43136, 43049, 42961, 42874, 42787, 42700,
    42613, 42527, 42440, 42354, 42268, 42182, 42097, 42011, 41926, 41841,
    41756, 41671, 41586, 41502, 41418, 41334, 41250, 41166, 41082, 40999,
    40916, 40832, 40750, 40667, 40584, 40502, 40420, 40338, 40256, 40174,
    40092, 40011, 39930, 39849, 39768, 39687, 39606, 39526, 39446, 39365,
    39286, 39206, 39126, 39047, 38967, 38888, 38809, 38731, 38652, 38573,
    38495, 38417, 38339, 38261, 38183, 38106, 38028, 37951, 37874, 37797,
    37720, 37644, 37567, 37491, 37415, 37339, 37263, 37188, 37112, 37037,
    36961, 36886, 36811, 36737, 36662, 36588, 36513, 36439, 36365, 36291,
    36218, 36144, 36071, 35998, 35924, 35851, 35779, 35706, 35634, 35561,
    35489, 35417, 35345, 35273, 35202, 35130, 35059, 34988, 34916, 34846,
    34775, 34704, 34634, 34563, 34493, 34423, 34353, 34284, 34214, 34144,
    34075, 34006, 33937, 33868, 33799, 33731, 33662, 33594, 33525, 33457,
    33389, 33322, 33254, 33186, 33119, 33052, 32985, 32918, 32851, 32784,
    32718, 32651, 32585, 32519, 32453, 32387, 32321, 32255, 32190, 32124,
    32059, 31994, 31929, 31864, 31800, 31735, 31671, 31606, 31542, 31478,
    31414, 31350, 31287, 31223, 31160, 31096, 31033, 30970, 30907, 30845,
    30782
};

// Exponential delay lookup for 1V/oct pitch control
// in: 0-4095 (knob + CV combined)
// Returns delay in samples (right-shifted by octave)
int32_t ExpDelay(int32_t in) {
    if (in < 0) in = 0;
    if (in > 4091) in = 4091;
    int32_t oct = in / 341;
    int32_t suboct = in % 341;
    return delay_vals[suboct] >> oct;
}

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
        ADD9 = 6,        // 1:1, 5:4, 3:2, 9:4 (major add 9)
        TANPURA_PA = 7,  // 1:1, 3:2, 2:1, 4:1 (Sa, Pa, Sa', Sa'')
        TANPURA_MA = 8   // 1:1, 4:3, 2:1, 4:1 (Sa, Ma, Sa', Sa'')
    };
    static const int NUM_MODES = 9;
    ChordMode currentMode;
    bool lastSwitchDown;

    // Pulse excitation state
    int32_t pulseExciteEnvelope;
    uint32_t noiseState;

    // DC blocker states for each string
    int32_t dcState1, dcState2, dcState3, dcState4;

    // One-pole lowpass filter for damping
    int32_t dampingFilter(int32_t input, int32_t& state, int32_t coefficient) {
        state += (((input - state) * coefficient + 32768) >> 16);
        return state;
    }

    // Process one string with linear interpolation for fractional delay
    int32_t processString(int16_t* delayLine, int& writeIndex, int delayLength,
                         int32_t& filterState, int32_t& dcState, int32_t excitation,
                         int32_t dampingCoeff, int32_t frac) {
        // Read two adjacent samples from delay line
        int readIndex1 = writeIndex - delayLength;
        if (readIndex1 < 0) readIndex1 += MAX_DELAY_SIZE;
        int readIndex2 = readIndex1 - 1;
        if (readIndex2 < 0) readIndex2 += MAX_DELAY_SIZE;

        int32_t sample1 = delayLine[readIndex1];
        int32_t sample2 = delayLine[readIndex2];

        // Linear interpolation: blend based on fractional part (frac is 0-255)
        int32_t delayedSample = ((sample1 * (256 - frac)) + (sample2 * frac)) >> 8;

        // Apply damping filter
        int32_t dampedSample = dampingFilter(delayedSample, filterState, dampingCoeff);

        // DC blocker: remove DC offset to prevent accumulation
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
            case TANPURA_PA:
                // Tanpura Pa: 1:1, 3:2, 2:1, 4:1 (Sa, Pa, Sa', Sa'')
                num2 = 3; den2 = 2;
                num3 = 2; den3 = 1;
                num4 = 4; den4 = 1;
                break;
            case TANPURA_MA:
                // Tanpura Ma: 1:1, 4:3, 2:1, 4:1 (Sa, Ma, Sa', Sa'')
                num2 = 4; den2 = 3;
                num3 = 2; den3 = 1;
                num4 = 4; den4 = 1;
                break;
        }
    }

public:
    ResonatingStrings() : writeIndex1(0), writeIndex2(0), writeIndex3(0), writeIndex4(0),
                          delayLength1(100), delayLength2(150), delayLength3(200), delayLength4(400),
                          filterState1(0), filterState2(0), filterState3(0), filterState4(0),
                          currentMode(FIFTH), lastSwitchDown(true),
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

        // FREQUENCY CONTROL (X Knob + CV1) - 1V/oct
        int32_t frequencyKnob = KnobVal(X);  // 0-4095 sets base pitch / transpose
        int16_t cv1 = CVIn1();               // CV input (unipolar or bipolar)

        // Combine knob and CV
        int32_t pitchCV = frequencyKnob + cv1;
        if (pitchCV > 4095) pitchCV = 4095;
        if (pitchCV < 0) pitchCV = 0;

        // Get delay from exponential lookup table (1V/oct)
        int32_t baseDelay = ExpDelay(pitchCV);

        // Clamp to usable range
        const int MIN_DELAY = 15;
        const int MAX_DELAY = 960;
        if (baseDelay < MIN_DELAY) baseDelay = MIN_DELAY;
        if (baseDelay > MAX_DELAY) baseDelay = MAX_DELAY;

        // Get frequency ratios based on current chord mode
        // Using integer numerator/denominator to avoid floating-point
        int num1 = 1, den1 = 1, num2 = 2, den2 = 1, num3 = 3, den3 = 1, num4 = 4, den4 = 1;
        getFrequencyRatios(num1, den1, num2, den2, num3, den3, num4, den4);

        // Calculate delay lengths for each string using fixed-point math
        // delay = baseDelay * denominator / numerator
        // Use 8 extra bits of precision to extract fractional part for interpolation
        int32_t delayFull1 = ((baseDelay * den1) << 8) / num1;
        int32_t delayFull2 = ((baseDelay * den2) << 8) / num2;
        int32_t delayFull3 = ((baseDelay * den3) << 8) / num3;
        int32_t delayFull4 = ((baseDelay * den4) << 8) / num4;

        delayLength1 = delayFull1 >> 8;  // Integer part
        delayLength2 = delayFull2 >> 8;
        delayLength3 = delayFull3 >> 8;
        delayLength4 = delayFull4 >> 8;

        int32_t frac1 = delayFull1 & 0xFF;  // Fractional part (0-255)
        int32_t frac2 = delayFull2 & 0xFF;
        int32_t frac3 = delayFull3 & 0xFF;
        int32_t frac4 = delayFull4 & 0xFF;

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
        int32_t dampingCoeff = 32000 + ((dampingKnob * 33300) / 4095);

        // Excitation amounts for each string
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

        // Process each string with fractional delay interpolation
        int32_t out1 = processString(delayLine1, writeIndex1, delayLength1,
                                     filterState1, dcState1, excitation1, dampingCoeff, frac1);
        int32_t out2 = processString(delayLine2, writeIndex2, delayLength2,
                                     filterState2, dcState2, excitation2, dampingCoeff, frac2);
        int32_t out3 = processString(delayLine3, writeIndex3, delayLength3,
                                     filterState3, dcState3, excitation3, dampingCoeff, frac3);
        int32_t out4 = processString(delayLine4, writeIndex4, delayLength4,
                                     filterState4, dcState4, excitation4, dampingCoeff, frac4);

        // Mix strings together - stereo mid/side
        // Out1 (mid): all strings summed - mono compatible
        // Out2 (side): strings 1&3 center, strings 2&4 wide/diffuse
        int32_t resonatorOut1 = (out1 + out2 + out3 + out4) / 4;
        int32_t resonatorOut2 = (out1 - out2 + out3 - out4) / 4;

        // WET/DRY MIX (Main Knob)
        int32_t mixKnob = KnobVal(Main);  // 0-4095

        int32_t dryGain = 4095 - mixKnob;
        int32_t wetGain = mixKnob;

        int32_t mixedOutput1 = ((audioIn * dryGain) + (resonatorOut1 * wetGain) + 2048) >> 12;
        int32_t mixedOutput2 = ((audioIn * dryGain) + (resonatorOut2 * wetGain) + 2048) >> 12;

        // Clipping
        if (mixedOutput1 > 2047) mixedOutput1 = 2047;
        if (mixedOutput1 < -2047) mixedOutput1 = -2047;
        if (mixedOutput2 > 2047) mixedOutput2 = 2047;
        if (mixedOutput2 < -2047) mixedOutput2 = -2047;

        // Stereo output
        AudioOut1((int16_t)mixedOutput1);
        AudioOut2((int16_t)mixedOutput2);

        // LED indicators - all 6 LEDs show chord mode
        // LED 0: HARMONIC, LED 1: FIFTH, LED 2: MAJOR7
        // LED 3: MINOR7, LED 4: DIM, LED 5: SUS4
        // ADD9 (mode 6): LEDs 0+5, TANPURA_PA (mode 7): LEDs 1+4, TANPURA_MA (mode 8): LEDs 2+3
        LedOn(0, currentMode == HARMONIC || currentMode == ADD9);
        LedOn(1, currentMode == FIFTH || currentMode == TANPURA_PA);
        LedOn(2, currentMode == MAJOR7 || currentMode == TANPURA_MA);
        LedOn(3, currentMode == MINOR7 || currentMode == TANPURA_MA);
        LedOn(4, currentMode == DIM || currentMode == TANPURA_PA);
        LedOn(5, currentMode == SUS4 || currentMode == ADD9);
    }
};

int main() {
    static ResonatingStrings resonator;
    resonator.Run();
    return 0;
}
