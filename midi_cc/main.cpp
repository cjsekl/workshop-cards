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
     Main Knob -> CV Out 1 + MIDI CC 34
     Knob X    -> CV Out 2 + MIDI CC 35
     Knob Y    -> Audio Out 1 + MIDI CC 36  (AC-coupled, drifts to 0V when stationary)
     Switch    -> MIDI CC 37                 (3 positions: 0/63/127)
     Audio In 1 -> MIDI CC 39  (envelope follower, ~85ms release)
     Audio In 2 -> MIDI CC 38  (zero-crossing rate, 20ms window)
     CV In 1    -> MIDI CC 40
     CV In 2    -> MIDI CC 41
     Pulse In 1 -> MIDI CC 42  (rising edge → full scale, else 0)
*/

class MIDICCCard : public ComputerCard
{
	// Processed audio values (written by Core 0, read by Core 1)
	volatile uint16_t audioIn1Envelope = 0;
	volatile uint16_t audioIn2ZCR = 0;
	volatile uint16_t pulseIn1Val = 0;

	// Envelope follower state (Core 0 only)
	int32_t envState = 0;

	// ZCR state (Core 0 only)
	bool lastSign2 = false;
	int32_t zcrCount = 0;
	int32_t zcrWindowCount = 0;

public:
	MIDICCCard()
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

		tusb_init();

		uint32_t lastSendTime = 0;

		while (1)
		{
			tud_task();

			// Drain any incoming MIDI (we don't use it, but must read to keep USB happy)
			while (tud_midi_available())
			{
				tud_midi_stream_read(buffer, sizeof(buffer));
			}

			// Send MIDI CC at ~1kHz rate
			uint32_t now = time_us_32();
			if (now - lastSendTime >= 1000)
			{
				lastSendTime = now;

				// Read all inputs (audio channels use processed values from Core 0)
				uint16_t values[WS_NUM_CHANNELS] = {
					audioIn1Envelope,
					audioIn2ZCR,
					(uint16_t)(CVIn1() + 2048),
					(uint16_t)(CVIn2() + 2048),
					(uint16_t)KnobVal(Knob::Main),
					(uint16_t)KnobVal(Knob::X),
					(uint16_t)KnobVal(Knob::Y),
					(uint16_t)(SwitchVal() * 2047),
					pulseIn1Val,
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
		// Pulse In 1: rising edge → full scale, otherwise 0
		if (PulseIn1RisingEdge()) {
			pulseIn1Val = 4095;
		} else if (pulseIn1Val > 0) {
			pulseIn1Val = 0;
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

		// Read knob values (0-4095)
		int32_t mainKnob = KnobVal(Knob::Main);
		int32_t xKnob = KnobVal(Knob::X);
		int32_t yKnob = KnobVal(Knob::Y);

		// Output knob values on CV/audio outputs (map 0-4095 to -2048..2047)
		CVOut1(mainKnob - 2048);
		CVOut2(xKnob - 2048);
		AudioOut1(yKnob - 2048);

		// LED feedback: knob brightness on LEDs 0, 2, 4
		LedBrightness(0, mainKnob >> 1);  // 0-2047 range
		LedBrightness(2, xKnob >> 1);
		LedBrightness(4, yKnob >> 1);

		// Heartbeat on LED 5
		static int32_t frame = 0;
		LedOn(5, (frame >> 13) & 1);
		frame++;
	}
};

int main()
{
	MIDICCCard card;
	card.Run();
}
