#include "ComputerCard.h"

// Audio delay for Music Thing Modular Workshop System
class AudioDelay : public ComputerCard
{
private:
    // Delay buffer parameters
    static const int MAX_DELAY_SIZE = 96000;  // 2.0 seconds at 48kHz
    int16_t delayBuffer[MAX_DELAY_SIZE];
    int writeIndex;

    // Control smoothing
    int32_t smoothedDelay;
    int32_t lastRawControl;  // For hysteresis
    int32_t ledCounter;

    // Mode selection
    enum DelayMode { CLEAN = 0, SATURATION = 1, SHIMMER = 2, LOFI = 3 };
    DelayMode currentMode;
    bool lastSwitchDown;

    // Filter states
    int32_t hpfState;
    int32_t shimmerHpfState;
    int32_t saturationAccum;

    // Tap tempo state (Pulse In 1)
    uint32_t lastTapTime;
    uint32_t tapInterval;
    uint32_t tapTimeout;
    bool tapTempoActive;
    bool lastPulse1;
    uint32_t sampleCounter;

    // Freeze state
    bool lastFreezeActive;
    int32_t frozenWritePos;
    int32_t frozenDelayTimeL;
    int32_t frozenDelayTimeR;

    int32_t highpass(int32_t input) {
        // One-pole highpass filter with coefficient b = 200
        // *state += (((input - *state) * b) >> 16)
        // return input - *state
        hpfState += (((input - hpfState) * 200 + 32768) >> 16);
        return input - hpfState;
    }

    int32_t shimmerHighpass(int32_t input) {
        shimmerHpfState += (((input - shimmerHpfState) * 1200 + 32768) >> 16);
        return input - shimmerHpfState;
    }

    void clip(int32_t &a) {
        if (a < -2047) a = -2047;
        if (a > 2047) a = 2047;
    }

    int32_t warmSaturate(int32_t input) {
        // Progressive saturation - slowly track signal energy with cap
        int32_t absInput = (input < 0) ? -input : input;
        saturationAccum = ((252 * saturationAccum + 128) >> 8) + ((absInput + 128) >> 8);

        if (saturationAccum > 400) saturationAccum = 400;

        int32_t drive = 2700 + ((saturationAccum + 8) >> 4);

        int64_t driven = ((int64_t)input * drive + 1024) >> 11;

        int32_t output;

        const int32_t softKnee = 600;

        if (driven >= 0) {
            if (driven < softKnee) {
                output = driven;
            } else {
                int32_t excess = driven - softKnee;
                output = softKnee + ((excess + 4) >> 3) + ((excess + 16) >> 5);  // ~31.25% of excess
                if (output > 2047) output = 2047;
            }
        } else {
            int32_t posInput = -driven;
            if (posInput < softKnee) {
                output = driven;
            } else {
                int32_t excess = posInput - softKnee;
                output = -(softKnee + ((excess + 4) >> 3) + ((excess + 16) >> 5));
                if (output < -2047) output = -2047;
            }
        }

        output = (int32_t)(((int64_t)output * 1434 + 1024) >> 11);

        return output;
    }

public:
    AudioDelay() : writeIndex(0), smoothedDelay(0), lastRawControl(0), ledCounter(0),
                   currentMode(CLEAN), lastSwitchDown(true),
                   hpfState(0), shimmerHpfState(0), saturationAccum(0),
                   lastTapTime(0), tapInterval(24000), tapTimeout(0), tapTempoActive(false),
                   lastPulse1(false), sampleCounter(0),
                   lastFreezeActive(false), frozenWritePos(0), frozenDelayTimeL(0), frozenDelayTimeR(0) {
    }

protected:
    void ProcessSample() override {

        int16_t audioIn1 = AudioIn1();
        int16_t audioIn2 = AudioIn2();

        int32_t audioIn = ((int32_t)audioIn1 + (int32_t)audioIn2 + 1) >> 1;

        Switch switchPos = SwitchVal();
        bool switchDown = (switchPos == Down);
        if (switchDown && !lastSwitchDown) {
            currentMode = (DelayMode)((currentMode + 1) % 4);
        }
        lastSwitchDown = switchDown;

        // TAP TEMPO
        bool pulse1 = PulseIn1();
        if (pulse1 && !lastPulse1) {
            // Rising edge detected - new tap
            uint32_t timeSinceLastTap = sampleCounter - lastTapTime;

            // Only accept taps within reasonable range (50ms to 3 seconds)
            if (timeSinceLastTap >= 2400 && timeSinceLastTap <= 144000) {
                tapInterval = timeSinceLastTap;
                tapTempoActive = true;
                tapTimeout = sampleCounter + 240000;
            }
            lastTapTime = sampleCounter;
        }
        lastPulse1 = pulse1;

        // Timeout: If no tap for 5 seconds, return to knob control
        if (tapTempoActive && (int32_t)(sampleCounter - tapTimeout) >= 0) {
            tapTempoActive = false;
        }

        sampleCounter++;

        int32_t delayKnob = KnobVal(X);

        int16_t cv1 = CVIn1();

        // DELAY TIME
        int32_t combinedControl = delayKnob + cv1;
        if (combinedControl > 4095) combinedControl = 4095;
        if (combinedControl < 0) combinedControl = 0;

        // Apply hysteresis to prevent ADC noise from causing micro-modulation
        // Only update if change is significant (threshold of 8 out of 4095 = ~0.2%)
        if (currentMode != LOFI) {
            const int32_t HYSTERESIS_THRESHOLD = 8;
            int32_t controlDelta = combinedControl - lastRawControl;
            if (controlDelta < 0) controlDelta = -controlDelta;

            if (controlDelta >= HYSTERESIS_THRESHOLD) {
                lastRawControl = combinedControl;
            } else {
                combinedControl = lastRawControl;
            }
        } else {
            // LOFI mode: no hysteresis
            lastRawControl = combinedControl;
        }

        const int32_t MIN_DELAY = 100;
        const int32_t MAX_DELAY = 95000;

        int32_t targetDelay;
        if (tapTempoActive) {
            // Tap tempo mode: Use measured tap interval
            targetDelay = tapInterval;
            if (targetDelay < MIN_DELAY) targetDelay = MIN_DELAY;
            if (targetDelay > MAX_DELAY) targetDelay = MAX_DELAY;
        } else {
            // Manual mode: Use knob + CV
            int32_t delayRange = MAX_DELAY - MIN_DELAY;
            targetDelay = MIN_DELAY + (combinedControl * delayRange) / 4095;
        }

        int32_t targetDelayFine = targetDelay << 7;

        // Exponential smoothing
        smoothedDelay = (int32_t)(((int64_t)smoothedDelay * 255 + targetDelayFine + 128) >> 8);

        // SHIMMER MODE: Fixed pitch shift of +7 semitones
        int32_t pitchModulation = 0;
        if (currentMode == SHIMMER) {
            // INITIAL SHIFT: +7 semitones = perfect fifth up
            //   Ratio = 2^(7/12) = 1.4983
            //   Delay = 1/1.4983 = 0.6674 = -33.26% change
            //   Fixed point: -21782
            //
            //   Each feedback repeat adds +7 semitones (perfect fifth)
            //   Input: original pitch (0)
            //   1st echo: +7 semitones (perfect fifth)
            //   2nd echo: +14 semitones (major ninth)
            //   3rd echo: +21 semitones (octave + major sixth)
            //   4th echo: +28 semitones (2 octaves + perfect fourth)
            //   5th echo: +35 semitones (2 octaves + major ninth)

            int32_t pitchMod = -21782;

            int64_t temp = (int64_t)smoothedDelay * pitchMod;
            pitchModulation = (int32_t)((temp + (temp >= 0 ? 32768 : -32768)) >> 16);
        }

        int32_t modulatedDelay = smoothedDelay + pitchModulation;

        // Clamp modulated delay to valid range
        int32_t minDelayFine = MIN_DELAY << 7;
        int32_t maxDelayFine = MAX_DELAY << 7;
        if (modulatedDelay < minDelayFine) modulatedDelay = minDelayFine;
        if (modulatedDelay > maxDelayFine) modulatedDelay = maxDelayFine;

        // STEREO
        int32_t modulatedDelayRight;
        if (currentMode == CLEAN || currentMode == LOFI) {
            modulatedDelayRight = modulatedDelay;
        } else if (currentMode == SATURATION) {
            // SATURATION mode: 1% stereo offset
            modulatedDelayRight = (int32_t)(((int64_t)modulatedDelay * 101) / 100);
        } else {
            // SHIMMER mode: 10% stereo offset
            modulatedDelayRight = (int32_t)(((int64_t)modulatedDelay * 110) / 100);
        }

        if (modulatedDelayRight < minDelayFine) modulatedDelayRight = minDelayFine;
        if (modulatedDelayRight > maxDelayFine) modulatedDelayRight = maxDelayFine;

        // FREEZE DETECTION
        bool freezeActive = PulseIn2();
        if (freezeActive && !lastFreezeActive) {
            frozenWritePos = writeIndex;
            frozenDelayTimeL = modulatedDelay >> 7;
            frozenDelayTimeR = modulatedDelayRight >> 7;
        }
        lastFreezeActive = freezeActive;

        int32_t delayInSamplesLeft, delayInSamplesRight;
        int32_t effectiveWriteIndex;

        if (freezeActive) {
            int32_t advancedSamples = writeIndex - frozenWritePos;
            if (advancedSamples < 0) advancedSamples += MAX_DELAY_SIZE;

            delayInSamplesLeft = frozenDelayTimeL;
            delayInSamplesRight = frozenDelayTimeR;

            effectiveWriteIndex = frozenWritePos + (advancedSamples % (frozenDelayTimeL + 1));
            if (effectiveWriteIndex >= MAX_DELAY_SIZE) effectiveWriteIndex -= MAX_DELAY_SIZE;
        } else {
            delayInSamplesLeft = modulatedDelay >> 7;
            delayInSamplesRight = modulatedDelayRight >> 7;
            effectiveWriteIndex = writeIndex;
        }

        int32_t fractionLeft = modulatedDelay & 0x7F;

        int32_t readIndex1L = effectiveWriteIndex - delayInSamplesLeft - 1;
        int32_t readIndex2L = effectiveWriteIndex - delayInSamplesLeft - 2;

        if (readIndex1L < 0) readIndex1L += MAX_DELAY_SIZE;
        if (readIndex2L < 0) readIndex2L += MAX_DELAY_SIZE;
        readIndex1L = readIndex1L % MAX_DELAY_SIZE;
        readIndex2L = readIndex2L % MAX_DELAY_SIZE;

        int32_t sample1L = delayBuffer[readIndex1L];
        int32_t sample2L = delayBuffer[readIndex2L];
        int32_t delayedSampleLeft = (sample2L * fractionLeft + sample1L * (128 - fractionLeft) + 64) >> 7;

        int32_t fractionRight = modulatedDelayRight & 0x7F;

        int32_t readIndex1R = effectiveWriteIndex - delayInSamplesRight - 1;
        int32_t readIndex2R = effectiveWriteIndex - delayInSamplesRight - 2;

        if (readIndex1R < 0) readIndex1R += MAX_DELAY_SIZE;
        if (readIndex2R < 0) readIndex2R += MAX_DELAY_SIZE;
        readIndex1R = readIndex1R % MAX_DELAY_SIZE;
        readIndex2R = readIndex2R % MAX_DELAY_SIZE;

        int32_t sample1R = delayBuffer[readIndex1R];
        int32_t sample2R = delayBuffer[readIndex2R];
        int32_t delayedSampleRight = (sample2R * fractionRight + sample1R * (128 - fractionRight) + 64) >> 7;

        int32_t delayedSample = delayedSampleLeft;

        // FEEDBACK
        int32_t feedbackKnob = KnobVal(Y);

        int16_t cv2 = CVIn2();

        int32_t combinedFeedback = feedbackKnob + cv2;
        if (combinedFeedback > 4095) combinedFeedback = 4095;
        if (combinedFeedback < 0) combinedFeedback = 0;

        int32_t inputGain = 4095 - ((combinedFeedback * combinedFeedback + 2048) >> 12);
        int32_t feedbackGain = 4095 - (((4095 - combinedFeedback) * (4095 - combinedFeedback) + 2048) >> 12);

        const int32_t MIN_INPUT_GAIN = 205;  // ~5% of 4095
        if (inputGain < MIN_INPUT_GAIN) inputGain = MIN_INPUT_GAIN;

        int32_t feedbackSignal = (delayedSample * feedbackGain + 2048) >> 12;

        if (currentMode == SATURATION) {
            feedbackSignal = warmSaturate(feedbackSignal);
            int32_t dynamicGain;
            if (saturationAccum < 150) {
                dynamicGain = 1740 + (saturationAccum << 2);
            } else {
                int32_t decay = saturationAccum - 150;
                dynamicGain = 2340 - ((decay * 5 + 1) >> 1);
                if (dynamicGain < 1126) dynamicGain = 1126;
            }

            feedbackSignal = (int32_t)(((int64_t)feedbackSignal * dynamicGain + 1024) >> 11);
        } else if (currentMode == SHIMMER) {
            feedbackSignal = shimmerHighpass(feedbackSignal);
        }

        int32_t mixedSignal = ((audioIn * inputGain + 2048) >> 12) + feedbackSignal;

        int32_t filteredSignal = highpass(mixedSignal);

        if (filteredSignal > 2047) filteredSignal = 2047;
        if (filteredSignal < -2047) filteredSignal = -2047;

        if (!freezeActive) {
            delayBuffer[writeIndex] = (int16_t)filteredSignal;
        }

        writeIndex = (writeIndex + 1) % MAX_DELAY_SIZE;

        int32_t mixKnob = KnobVal(Main);  // 0-4095

        int32_t dryGain = 4095 - mixKnob;
        int32_t wetGain = mixKnob;

        int32_t mixedOutputLeft = ((audioIn * dryGain) + (delayedSampleLeft * wetGain) + 2048) >> 12;
        clip(mixedOutputLeft);

        int32_t mixedOutputRight = ((audioIn * dryGain) + (delayedSampleRight * wetGain) + 2048) >> 12;
        clip(mixedOutputRight);

        int16_t outputLeft = (int16_t)mixedOutputLeft;
        int16_t outputRight = (int16_t)mixedOutputRight;

        AudioOut1(outputLeft);
        AudioOut2(outputRight);

        // LEDs
        ledCounter++;
        int32_t blinkRate = delayInSamplesLeft / 2;
        if (blinkRate < 100) blinkRate = 100;

        // LED 0: Delay time indicator
        if (ledCounter >= blinkRate) {
            ledCounter = 0;
            LedOn(0, true);
        } else if (ledCounter >= blinkRate / 2) {
            LedOn(0, false);
        }

        // LED 1: Feedback amount indicator (on when > 50%)
        if (combinedFeedback > 2048) {
            LedOn(1, true);
        } else {
            LedOn(1, false);
        }

        // LEDs 2-5: Mode indicators
        // LED 2: CLEAN mode
        // LED 3: SATURATION mode
        // LED 4: SHIMMER mode
        // LED 5: LOFI mode
        LedOn(2, currentMode == CLEAN);
        LedOn(3, currentMode == SATURATION);
        LedOn(4, currentMode == SHIMMER);
        LedOn(5, currentMode == LOFI);
    }
};

int main() {
    static AudioDelay delay;
    delay.Run();
    return 0;
}
