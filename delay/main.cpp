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
    int32_t lastRawControl;  // For hysteresis to prevent ADC noise micro-modulation
    int32_t ledCounter;

    // Mode selection
    enum DelayMode { CLEAN = 0, SATURATION = 1, SHIMMER = 2, LOFI = 3 };
    DelayMode currentMode;
    bool lastSwitchDown;

    // Filter states
    int32_t hpfState;    // Highpass filter state for DC offset removal
    int32_t shimmerHpfState;  // Highpass filter state for shimmer brightness
    int32_t saturationAccum;  // Accumulator for progressive saturation

    // Tap tempo state (Pulse In 1)
    uint32_t lastTapTime;      // Timestamp of last tap (in samples)
    uint32_t tapInterval;      // Measured interval between taps (in samples)
    uint32_t tapTimeout;       // Timeout deadline for tap tempo (overflow-safe)
    bool tapTempoActive;       // Whether tap tempo is controlling delay time
    bool lastPulse1;           // Previous pulse state for edge detection
    uint32_t sampleCounter;    // Global sample counter for timing

    // Freeze state
    bool lastFreezeActive;     // Previous freeze state for edge detection
    int32_t frozenWritePos;    // Write position when freeze was activated
    int32_t frozenDelayTimeL;  // Left channel delay time when freeze was activated
    int32_t frozenDelayTimeR;  // Right channel delay time when freeze was activated

    int32_t highpass(int32_t input) {
        // One-pole highpass filter with coefficient b = 200
        // *state += (((input - *state) * b) >> 16)
        // return input - *state
        hpfState += (((input - hpfState) * 200 + 32768) >> 16);
        return input - hpfState;
    }

    int32_t shimmerHighpass(int32_t input) {
        // Aggressive highpass filter for shimmer mode to emphasize upper harmonics
        // Higher coefficient (1200) = more aggressive filtering, brighter sound
        // Emphasizes the perfect fifth harmonics and reduces muddy low end
        shimmerHpfState += (((input - shimmerHpfState) * 1200 + 32768) >> 16);
        return input - shimmerHpfState;
    }

    // Hard clipping for output stage
    void clip(int32_t &a) {
        if (a < -2047) a = -2047;
        if (a > 2047) a = 2047;
    }

    int32_t warmSaturate(int32_t input) {
        // Progressive saturation - slowly track signal energy with cap
        int32_t absInput = (input < 0) ? -input : input;
        saturationAccum = ((252 * saturationAccum + 128) >> 8) + ((absInput + 128) >> 8);

        // Cap accumulator to prevent runaway (max ~400)
        if (saturationAccum > 400) saturationAccum = 400;

        // High drive with controlled progression for crunch without runaway
        // Max drive = 2750 ensures stability even below soft knee
        int32_t drive = 2700 + ((saturationAccum + 8) >> 4);

        // Scale input by drive
        int64_t driven = ((int64_t)input * drive + 1024) >> 11;

        // Very aggressive soft saturation for rich harmonics and crunch
        // Compresses hard above knee to control level while adding character
        int32_t output;

        // Very low soft knee for early, aggressive saturation
        const int32_t softKnee = 600;  // Very early saturation for maximum crunch

        if (driven >= 0) {
            if (driven < softKnee) {
                output = driven;  // Clean pass-through for low levels
            } else {
                // Very aggressive soft saturation curve with extreme compression
                int32_t excess = driven - softKnee;
                // Compress excess very heavily: ~31.25% of excess passes through
                output = softKnee + ((excess + 4) >> 3) + ((excess + 16) >> 5);  // ~31.25% of excess
                if (output > 2047) output = 2047;
            }
        } else {
            // Symmetric for both sides (even-order harmonics)
            int32_t posInput = -driven;
            if (posInput < softKnee) {
                output = driven;  // Clean pass-through
            } else {
                int32_t excess = posInput - softKnee;
                output = -(softKnee + ((excess + 4) >> 3) + ((excess + 16) >> 5));
                if (output < -2047) output = -2047;
            }
        }

        // Post-saturation makeup gain to ensure net <1.0 gain for stability
        // Multiply by 0.7 (1434/2048) to guarantee feedback decay even with higher drive
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

        // Read both audio inputs and mix them
        int16_t audioIn1 = AudioIn1();
        int16_t audioIn2 = AudioIn2();

        // Mix both inputs together (sum and average to prevent clipping)
        int32_t audioIn = ((int32_t)audioIn1 + (int32_t)audioIn2 + 1) >> 1;

        // MODE CYCLING: Check switch down (momentary) to cycle modes
        Switch switchPos = SwitchVal();
        bool switchDown = (switchPos == Down);
        if (switchDown && !lastSwitchDown) {
            currentMode = (DelayMode)((currentMode + 1) % 4);
        }
        lastSwitchDown = switchDown;

        // TAP TEMPO: Pulse In 1 sets delay time rhythmically
        bool pulse1 = PulseIn1();
        if (pulse1 && !lastPulse1) {
            // Rising edge detected - new tap
            uint32_t timeSinceLastTap = sampleCounter - lastTapTime;

            // Only accept taps within reasonable range (50ms to 3 seconds)
            // 50ms = 2400 samples, 3s = 144000 samples
            if (timeSinceLastTap >= 2400 && timeSinceLastTap <= 144000) {
                tapInterval = timeSinceLastTap;
                tapTempoActive = true;
                // Set timeout deadline (5 seconds from now, overflow-safe)
                tapTimeout = sampleCounter + 240000;
            }
            lastTapTime = sampleCounter;
        }
        lastPulse1 = pulse1;

        // Timeout: If no tap for 5 seconds, return to knob control
        // Use signed comparison for overflow-safe timeout check
        if (tapTempoActive && (int32_t)(sampleCounter - tapTimeout) >= 0) {
            tapTempoActive = false;
        }

        sampleCounter++;

        // Read X knob for delay time control (0-4095)
        int32_t delayKnob = KnobVal(X);

        int16_t cv1 = CVIn1();

        // Combine knob and CV input
        // CV adds ±2048 to the knob value, clamped to valid range
        int32_t combinedControl = delayKnob + cv1;
        if (combinedControl > 4095) combinedControl = 4095;
        if (combinedControl < 0) combinedControl = 0;

        // Apply hysteresis to prevent ADC noise from causing micro-modulation
        // Only update if change is significant (threshold of 8 out of 4095 = ~0.2%)
        // This eliminates pitch wobble artifacts from tiny knob jitter
        // LOFI mode skips hysteresis to allow micro-modulation for lo-fi character
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

        // Map combined value to delay time in samples
        // Range: 100 samples (2ms) to 95000 samples (~2.0 seconds)
        const int32_t MIN_DELAY = 100;
        const int32_t MAX_DELAY = 95000;

        // Calculate delay time
        int32_t targetDelay;
        if (tapTempoActive) {
            // Tap tempo mode: Use measured tap interval
            // Clamp to valid delay range
            targetDelay = tapInterval;
            if (targetDelay < MIN_DELAY) targetDelay = MIN_DELAY;
            if (targetDelay > MAX_DELAY) targetDelay = MAX_DELAY;
        } else {
            // Manual mode: Use knob + CV
            int32_t delayRange = MAX_DELAY - MIN_DELAY;
            targetDelay = MIN_DELAY + (combinedControl * delayRange) / 4095;
        }

        int32_t targetDelayFine = targetDelay << 7;  // multiply by 128

        // Exponential smoothing
        smoothedDelay = (int32_t)(((int64_t)smoothedDelay * 255 + targetDelayFine + 128) >> 8);

        // SHIMMER MODE: Fixed pitch shift of +7 semitones (perfect fifth)
        // Stacks +7 semitones per feedback iteration for dense harmonic cascade
        int32_t pitchModulation = 0;
        if (currentMode == SHIMMER) {
            // INITIAL SHIFT: +7 semitones = perfect fifth up
            //   Ratio = 2^(7/12) = 1.4983
            //   Delay = 1/1.4983 = 0.6674 = -33.26% change
            //   Fixed point: -21782
            //
            // Effect: Each feedback repeat adds +7 semitones (perfect fifth)
            //   Input: original pitch (0)
            //   1st echo: +7 semitones (perfect fifth)
            //   2nd echo: +14 semitones (major ninth)
            //   3rd echo: +21 semitones (octave + major sixth)
            //   4th echo: +28 semitones (2 octaves + perfect fourth)
            //   5th echo: +35 semitones (2 octaves + major ninth)
            //   Creates dense, cascading harmonic staircase

            int32_t pitchMod = -21782;

            // Apply fixed pitch shift with proper rounding for negative values
            int64_t temp = (int64_t)smoothedDelay * pitchMod;
            pitchModulation = (int32_t)((temp + (temp >= 0 ? 32768 : -32768)) >> 16);
        }

        int32_t modulatedDelay = smoothedDelay + pitchModulation;

        // Clamp modulated delay to valid range
        int32_t minDelayFine = MIN_DELAY << 7;
        int32_t maxDelayFine = MAX_DELAY << 7;
        if (modulatedDelay < minDelayFine) modulatedDelay = minDelayFine;
        if (modulatedDelay > maxDelayFine) modulatedDelay = maxDelayFine;

        // STEREO: Create two delay taps with slightly different times
        // Left channel: use the modulated delay time as-is
        // Right channel: offset varies by mode
        int32_t modulatedDelayRight;
        if (currentMode == CLEAN || currentMode == LOFI) {
            modulatedDelayRight = modulatedDelay;
        } else if (currentMode == SATURATION) {
            // SATURATION mode: 1% stereo offset for subtle width
            modulatedDelayRight = (int32_t)(((int64_t)modulatedDelay * 101) / 100);
        } else {
            // SHIMMER mode: 10% stereo offset for expansive, dramatic soundscape
            modulatedDelayRight = (int32_t)(((int64_t)modulatedDelay * 110) / 100);
        }

        // Clamp right channel delay to valid range
        if (modulatedDelayRight < minDelayFine) modulatedDelayRight = minDelayFine;
        if (modulatedDelayRight > maxDelayFine) modulatedDelayRight = maxDelayFine;

        // FREEZE DETECTION: Capture state when freeze is activated
        bool freezeActive = PulseIn2();
        if (freezeActive && !lastFreezeActive) {
            // Rising edge - freeze just activated
            // Capture current write position and delay times for seamless looping
            frozenWritePos = writeIndex;
            frozenDelayTimeL = modulatedDelay >> 7;  // Left delay in samples
            frozenDelayTimeR = modulatedDelayRight >> 7;  // Right delay in samples
        }
        lastFreezeActive = freezeActive;

        // DELAY READ: Use frozen parameters during freeze for seamless looping
        int32_t delayInSamplesLeft, delayInSamplesRight;
        int32_t effectiveWriteIndex;

        if (freezeActive) {
            // During freeze: Read from frozen loop
            // Calculate how far we've advanced since freeze started
            int32_t advancedSamples = writeIndex - frozenWritePos;
            if (advancedSamples < 0) advancedSamples += MAX_DELAY_SIZE;

            // Use frozen delay times and create seamless loop
            delayInSamplesLeft = frozenDelayTimeL;
            delayInSamplesRight = frozenDelayTimeR;

            // Read position loops within the frozen delay time
            // This ensures we always read the same loop segment
            effectiveWriteIndex = frozenWritePos + (advancedSamples % (frozenDelayTimeL + 1));
            if (effectiveWriteIndex >= MAX_DELAY_SIZE) effectiveWriteIndex -= MAX_DELAY_SIZE;
        } else {
            // Normal operation: Use current delay times and write position
            delayInSamplesLeft = modulatedDelay >> 7;
            delayInSamplesRight = modulatedDelayRight >> 7;
            effectiveWriteIndex = writeIndex;
        }

        // LEFT CHANNEL: Extract fractional parts
        int32_t fractionLeft = modulatedDelay & 0x7F;

        // Read indices for linear interpolation (LEFT)
        int32_t readIndex1L = effectiveWriteIndex - delayInSamplesLeft - 1;
        int32_t readIndex2L = effectiveWriteIndex - delayInSamplesLeft - 2;

        // Handle wraparound for left indices
        if (readIndex1L < 0) readIndex1L += MAX_DELAY_SIZE;
        if (readIndex2L < 0) readIndex2L += MAX_DELAY_SIZE;
        readIndex1L = readIndex1L % MAX_DELAY_SIZE;
        readIndex2L = readIndex2L % MAX_DELAY_SIZE;

        // Read and interpolate left channel
        int32_t sample1L = delayBuffer[readIndex1L];
        int32_t sample2L = delayBuffer[readIndex2L];
        int32_t delayedSampleLeft = (sample2L * fractionLeft + sample1L * (128 - fractionLeft) + 64) >> 7;

        // RIGHT CHANNEL: Extract fractional parts
        int32_t fractionRight = modulatedDelayRight & 0x7F;

        // Calculate read indices for linear interpolation (RIGHT)
        int32_t readIndex1R = effectiveWriteIndex - delayInSamplesRight - 1;
        int32_t readIndex2R = effectiveWriteIndex - delayInSamplesRight - 2;

        // Handle wraparound for right indices
        if (readIndex1R < 0) readIndex1R += MAX_DELAY_SIZE;
        if (readIndex2R < 0) readIndex2R += MAX_DELAY_SIZE;
        readIndex1R = readIndex1R % MAX_DELAY_SIZE;
        readIndex2R = readIndex2R % MAX_DELAY_SIZE;

        // Read and interpolate right channel
        int32_t sample1R = delayBuffer[readIndex1R];
        int32_t sample2R = delayBuffer[readIndex2R];
        int32_t delayedSampleRight = (sample2R * fractionRight + sample1R * (128 - fractionRight) + 64) >> 7;

        // Use left channel for feedback (mono feedback to avoid phase issues)
        int32_t delayedSample = delayedSampleLeft;

        // STEP 6: Read Y knob for feedback amount (0-4095)
        int32_t feedbackKnob = KnobVal(Y);

        // Read CV2 input for feedback modulation (-2048 to 2047)
        int16_t cv2 = CVIn2();

        // Combine Y knob and CV2 input for feedback control
        // CV adds ±2048 to the knob value, clamped to valid range
        int32_t combinedFeedback = feedbackKnob + cv2;
        if (combinedFeedback > 4095) combinedFeedback = 4095;
        if (combinedFeedback < 0) combinedFeedback = 0;

        // Quadratic crossfade between input and feedback
        // As feedback increases, input decreases - prevents level buildup
        // inputGain: high when feedback is low, low when feedback is high
        // feedbackGain: low when feedback is low, high when feedback is high
        int32_t inputGain = 4095 - ((combinedFeedback * combinedFeedback + 2048) >> 12);
        int32_t feedbackGain = 4095 - (((4095 - combinedFeedback) * (4095 - combinedFeedback) + 2048) >> 12);

        // Ensure minimum input gain even at max feedback (5% minimum)
        // This allows new signal to enter the delay buffer at all feedback settings
        const int32_t MIN_INPUT_GAIN = 205;  // ~5% of 4095
        if (inputGain < MIN_INPUT_GAIN) inputGain = MIN_INPUT_GAIN;

        // Calculate feedback signal (delayed output * feedback amount)
        int32_t feedbackSignal = (delayedSample * feedbackGain + 2048) >> 12;

        // APPLY MODE EFFECTS to feedback signal
        if (currentMode == SATURATION) {
            // SATURATION Mode: Progressive warm saturation with dynamics envelope
            feedbackSignal = warmSaturate(feedbackSignal);

            // Dynamic gain envelope: Builds up then decays for "bloom" effect
            // Creates excitement in early repeats, then natural decay
            // saturationAccum range: 0 to 400 (capped)
            int32_t dynamicGain;
            if (saturationAccum < 150) {
                // Rising phase: 0.85x → 1.15x (bloom/swell)
                // Gain increases with each repeat, creating buildup
                dynamicGain = 1740 + (saturationAccum << 2);  // 1740 to 2340 (~0.85x to 1.14x)
            } else {
                // Decay phase: 1.15x → 0.55x (compression/decay)
                // Heavy compression for long tail
                int32_t decay = saturationAccum - 150;  // 0 to 250
                dynamicGain = 2340 - ((decay * 5 + 1) >> 1);  // 2340 down to ~1715
                if (dynamicGain < 1126) dynamicGain = 1126;  // Floor at 0.55x
            }

            // Apply dynamic gain envelope (2048 = 1.0x)
            feedbackSignal = (int32_t)(((int64_t)feedbackSignal * dynamicGain + 1024) >> 11);
        } else if (currentMode == SHIMMER) {
            // SHIMMER Mode: Aggressive highpass filtering to emphasize upper harmonics
            // Makes the perfect fifth stacking more obvious and prevents muddy buildup
            feedbackSignal = shimmerHighpass(feedbackSignal);
        }
        // CLEAN and LOFI modes: no processing, pass through as-is

        // Mix input with feedback using crossfaded gains
        // Input is scaled down as feedback increases, maintaining stable levels
        int32_t mixedSignal = ((audioIn * inputGain + 2048) >> 12) + feedbackSignal;

        // Apply highpass filter to prevent DC offset buildup in feedback loop
        int32_t filteredSignal = highpass(mixedSignal);

        // Clamp to audio system range (±2047) before writing to buffer
        // This prevents cascading clipping distortion through feedback iterations
        if (filteredSignal > 2047) filteredSignal = 2047;
        if (filteredSignal < -2047) filteredSignal = -2047;

        // FREEZE/HOLD: Write to buffer only when not frozen
        // (freezeActive already read earlier for smoothing)
        if (!freezeActive) {
            delayBuffer[writeIndex] = (int16_t)filteredSignal;
        }

        // Advance write index with wraparound
        writeIndex = (writeIndex + 1) % MAX_DELAY_SIZE;

        // Dry/wet mixing with Main knob
        int32_t mixKnob = KnobVal(Main);  // 0-4095

        // Calculate dry and wet gains
        int32_t dryGain = 4095 - mixKnob;
        int32_t wetGain = mixKnob;

        // Mix dry (input) and wet (delayed) signals for LEFT channel
        int32_t mixedOutputLeft = ((audioIn * dryGain) + (delayedSampleLeft * wetGain) + 2048) >> 12;
        clip(mixedOutputLeft);

        // Mix dry (input) and wet (delayed) signals for RIGHT channel
        int32_t mixedOutputRight = ((audioIn * dryGain) + (delayedSampleRight * wetGain) + 2048) >> 12;
        clip(mixedOutputRight);

        int16_t outputLeft = (int16_t)mixedOutputLeft;
        int16_t outputRight = (int16_t)mixedOutputRight;

        // Output stereo audio
        AudioOut1(outputLeft);
        AudioOut2(outputRight);

        // LED indicators
        ledCounter++;
        int32_t blinkRate = delayInSamplesLeft / 2;  // Blink at half the delay time
        if (blinkRate < 100) blinkRate = 100;        // Prevent too fast blinking

        // LED 0: Delay time indicator (blinks at delay rate)
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
