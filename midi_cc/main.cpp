#include "ComputerCard.h"
#include "WorkshopMidiCC.h"
#include "pico/multicore.h"
#include "tusb.h"

/*
   Workshop System MIDI CC Card

   Reads all knobs, CV inputs, audio inputs, and switch.
   Sends values as MIDI CC over USB (matching Simple MIDI CC mapping).
   Simultaneously outputs knob values on CV and audio outputs.

   Core 0: ComputerCard audio loop (48kHz) — reads inputs, writes outputs, drives LEDs
   Core 1: TinyUSB MIDI device — converts values to MIDI CC, sends over USB

   Output mapping:
     Main Knob -> Audio Out 1 + MIDI CC 34
     Knob X    -> CV Out 1 + MIDI CC 35
     Knob Y    -> CV Out 2 + MIDI CC 36
     Switch    -> MIDI CC 37                 (momentary down toggles pass mode)
     Audio In 1 -> MIDI CC 39  (envelope follower, ~85ms release)
     Audio In 2 -> MIDI CC 38  (zero-crossing rate, 20ms window)
     CV In 1    -> MIDI CC 40
     CV In 2    -> MIDI CC 41
     Pulse In 1 -> MIDI CC 42  (rising edge → full scale, hold 10ms)
     Pulse In 2 -> MIDI CC 43  (gate: high = 4095, low = 0)
     Card ID    -> MIDI CC 44-48  (5 x 7-bit = 35 bits, sent once at startup)
*/

class MIDICCCard : public ComputerCard
{
	// Processed audio values (written by Core 0, read by Core 1)
	volatile uint16_t audioIn1Envelope = 0;
	volatile uint16_t audioIn2ZCR = 0;
	volatile uint16_t pulseIn1Val = 0;
	volatile uint16_t pulseIn2Val = 0;
	int32_t pulseHoldCount = 0;
	int32_t pulseHoldCount2 = 0;

	// CV input averaging (written by Core 0, read by Core 1)
	volatile int32_t cvIn1Avg = 0;
	volatile int32_t cvIn2Avg = 0;
	int32_t cvIn1Accum = 0;
	int32_t cvIn2Accum = 0;
	int32_t cvAccumCount = 0;

	// Envelope follower state (Core 0 only)
	int32_t envState = 0;

	// ZCR state (Core 0 only)
	bool lastSign2 = false;
	int32_t zcrCount = 0;
	int32_t zcrWindowCount = 0;

public:
	MIDICCCard() {}

	void LaunchUSBCore()
	{
		multicore_launch_core1(core1);
	}

	static void core1()
	{
		((MIDICCCard *)ThisPtr())->USBCore();
	}

	void USBCore()
	{
		WorkshopMidiCC midiCC;
		uint8_t buffer[64];

		// Split 64-bit unique card ID into 5 x 7-bit MIDI CC values (35 bits)
		uint64_t uid = UniqueCardID();
		uint32_t cardId35 = (uint32_t)(((uid >> 32) ^ uid) & 0x7FFFFFFFF);
		uint8_t cardIdCC[5];
		for (int i = 0; i < 5; i++) {
			cardIdCC[i] = (cardId35 >> (i * 7)) & 0x7F;
		}

		tusb_init();

		// Send card ID as CC 44-48 once USB is ready
		bool cardIdSent = false;
		uint32_t lastSendTime = 0;

		while (1)
		{
			tud_task();

			if (!cardIdSent && tud_midi_available() >= 0) {
				for (int i = 0; i < 5; i++) {
					uint8_t bytes[3] = {0xB0, (uint8_t)(44 + i), cardIdCC[i]};
					tud_midi_stream_write(0, bytes, 3);
				}
				cardIdSent = true;
			}

			// Drain any incoming MIDI (we don't use it, but must read to keep USB happy)
			while (tud_midi_available())
			{
				tud_midi_stream_read(buffer, sizeof(buffer));
			}

			// Send MIDI CC at ~100Hz rate
			uint32_t now = time_us_32();
			if (now - lastSendTime >= 10000)
			{
				lastSendTime = now;

				// Read all inputs (audio channels use processed values from Core 0)
				uint16_t values[WS_NUM_CHANNELS] = {
					audioIn1Envelope,
					audioIn2ZCR,
					(uint16_t)(cvIn1Avg + 2048),
					(uint16_t)(cvIn2Avg + 2048),
					(uint16_t)KnobVal(Knob::Main),
					(uint16_t)KnobVal(Knob::X),
					(uint16_t)KnobVal(Knob::Y),
					(uint16_t)(SwitchVal() * 2047),
					pulseIn1Val,
					pulseIn2Val,
				};

				int n = midiCC.update(values);
				for (int i = 0; i < n; i++)
				{
					uint8_t bytes[3];
					midiCC.message(i).pack(bytes);
					tud_midi_stream_write(0, bytes, 3);
				}
			}
		}
	}

	virtual void ProcessSample()
	{
		// Pulse In 1: rising edge → full scale, hold ~10ms for Core 1 + browser
		if (PulseIn1RisingEdge()) {
			pulseIn1Val = 4095;
			pulseHoldCount = 480;  // 10ms at 48kHz
		} else if (pulseHoldCount > 0) {
			pulseHoldCount--;
		} else if (pulseIn1Val > 0) {
			pulseIn1Val = 0;
		}

		// Pulse In 2: rising edge → full scale, hold ~10ms
		if (PulseIn2RisingEdge()) {
			pulseIn2Val = 4095;
			pulseHoldCount2 = 480;
		} else if (pulseHoldCount2 > 0) {
			pulseHoldCount2--;
		} else if (pulseIn2Val > 0) {
			pulseIn2Val = 0;
		}

		// Envelope follower on Audio In 1 (fixed-point <<8 for release precision)
		int32_t sample1 = AudioIn1();
		int32_t absVal = sample1 < 0 ? -sample1 : sample1;
		int32_t absScaled = absVal << 8;
		if (absScaled > envState) {
			envState = absScaled;
		} else {
			envState -= envState >> 12;
		}
		audioIn1Envelope = (uint16_t)(envState >> 7);

		// Zero-crossing rate on Audio In 2
		int32_t sample2 = AudioIn2();
		int32_t abs2 = sample2 < 0 ? -sample2 : sample2;
		bool currentSign = sample2 >= 0;
		if (abs2 > 30 && currentSign != lastSign2) zcrCount++;
		lastSign2 = currentSign;
		zcrWindowCount++;
		if (zcrWindowCount >= 960) {
			audioIn2ZCR = (uint16_t)(zcrCount * 50 > 4095 ? 4095 : zcrCount * 50);
			zcrCount = 0;
			zcrWindowCount = 0;
		}

		// Accumulate CV inputs for averaging (reduces noise on floating inputs)
		cvIn1Accum += CVIn1();
		cvIn2Accum += CVIn2();
		cvAccumCount++;
		if (cvAccumCount >= 480) {  // average over 10ms at 48kHz
			cvIn1Avg = cvIn1Accum / cvAccumCount;
			cvIn2Avg = cvIn2Accum / cvAccumCount;
			cvIn1Accum = 0;
			cvIn2Accum = 0;
			cvAccumCount = 0;
		}

		// Read knob values (0-4095)
		int32_t mainKnob = KnobVal(Knob::Main);
		int32_t xKnob = KnobVal(Knob::X);
		int32_t yKnob = KnobVal(Knob::Y);

		// Output knob values on CV/audio outputs (map 0-4095 to -2048..2047)
		AudioOut1(mainKnob - 2048);
		CVOut1(xKnob - 2048);
		CVOut2(yKnob - 2048);

		// LED feedback: knob brightness on LEDs 0, 2, 4
		LedBrightness(0, yKnob >> 1);    // 0-2047 range
		LedBrightness(2, xKnob >> 1);
		LedBrightness(4, mainKnob >> 1);

		// Heartbeat on LED 5
		static int32_t frame = 0;
		LedOn(5, (frame >> 13) & 1);
		frame++;
	}
};

int main()
{
	MIDICCCard card;
	card.EnableNormalisationProbe();
	card.LaunchUSBCore();
	card.Run();
}
