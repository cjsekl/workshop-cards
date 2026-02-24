/*
  WorkshopMidiCC.h - Pure C++ header (no Arduino, no hardware deps)

  Extracted from Simple MIDI v0.6.6 for Music Thing Modular Workshop System
  Original code by Tom Whitwell, released under MIT License.

  Converts Workshop System control values into MIDI CC messages.
  No I/O — you feed in values, you get MIDI bytes out.

  Usage with ComputerCard:

    #include "ComputerCard.h"
    #include "WorkshopMidiCC.h"

    class MyCard : public ComputerCard {
        WorkshopMidiCC midiCC;

        void ProcessSample() override {
            uint16_t vals[8] = {
                (uint16_t)(AudioIn1() + 2048),   // signed -> unsigned 12-bit
                (uint16_t)(AudioIn2() + 2048),
                (uint16_t)(CVIn1() + 2048),
                (uint16_t)(CVIn2() + 2048),
                (uint16_t)KnobVal(Main),          // already 0-4095
                (uint16_t)KnobVal(X),
                (uint16_t)KnobVal(Y),
                (uint16_t)(SwitchVal() * 2047),   // 0/1/2 -> 0/2047/4094
            };

            int n = midiCC.update(vals);
            for (int i = 0; i < n; i++) {
                uint8_t bytes[3];
                midiCC.message(i).pack(bytes);
                // send bytes over USB/UART/etc
            }
        }
    };
*/

#ifndef WORKSHOP_MIDI_CC_H
#define WORKSHOP_MIDI_CC_H

#include <cstdint>

// Channel indices matching the Workshop System hardware layout
enum WSChannel : uint8_t {
    WS_AUDIO_IN_1 = 0,
    WS_AUDIO_IN_2 = 1,
    WS_CV_IN_1    = 2,
    WS_CV_IN_2    = 3,
    WS_KNOB_MAIN  = 4,
    WS_KNOB_X     = 5,
    WS_KNOB_Y     = 6,
    WS_SWITCH     = 7,
    WS_PULSE_IN_1 = 8,
    WS_NUM_CHANNELS = 9,
};

// CC number assignments (from Simple MIDI documentation)
//   Audio In 1 -> CC 39    Audio In 2 -> CC 38
//   CV In 1    -> CC 40    CV In 2    -> CC 41
//   Main Knob  -> CC 34    Knob X     -> CC 35
//   Knob Y     -> CC 36    Switch     -> CC 37
//   Pulse In 1 -> CC 42
static constexpr uint8_t WS_CC_MAP[WS_NUM_CHANNELS] = {
    39, 38, 40, 41, 34, 35, 36, 37, 42
};

struct MidiCCMessage {
    uint8_t channel;  // MIDI channel (1-16)
    uint8_t cc;       // CC number
    uint8_t value;    // 0-127

    // Pack into 3 raw MIDI bytes (status, cc, value)
    void pack(uint8_t out[3]) const {
        out[0] = 0xB0 | ((channel - 1) & 0x0F);
        out[1] = cc & 0x7F;
        out[2] = value & 0x7F;
    }
};

class WorkshopMidiCC {
public:
    // midiChannel: which MIDI channel to tag messages with (1-16)
    WorkshopMidiCC(uint8_t midiChannel = 1)
        : _midiChannel(midiChannel), _pendingCount(0)
    {
        for (int i = 0; i < WS_NUM_CHANNELS; i++) _lastCC[i] = -1;
    }

    // Feed all 9 channel values as unsigned 12-bit (0-4095).
    // For signed ComputerCard values (AudioIn, CVIn: -2048..2047),
    // add 2048 before passing.
    // Returns number of CC messages generated this call.
    int update(const uint16_t values[WS_NUM_CHANNELS]) {
        _pendingCount = 0;
        for (int i = 0; i < WS_NUM_CHANNELS; i++) {
            uint8_t val7 = (values[i] > 4095 ? 4095 : values[i]) >> 5;
            if (val7 != _lastCC[i]) {
                _pending[_pendingCount++] = {_midiChannel, WS_CC_MAP[i], val7};
                _lastCC[i] = val7;
            }
        }
        return _pendingCount;
    }

    // Number of messages from last update()
    int messageCount() const { return _pendingCount; }

    // Access a message by index (0 .. messageCount()-1)
    const MidiCCMessage& message(int i) const { return _pending[i]; }

    // Pointer to message array
    const MidiCCMessage* messages() const { return _pending; }

    // Read the last sent 7-bit value for a channel, or -1 if never sent
    int8_t lastValue(uint8_t channel) const {
        return (channel < WS_NUM_CHANNELS) ? _lastCC[channel] : -1;
    }

    // Reset change tracking (next update will send all channels)
    void reset() {
        for (int i = 0; i < WS_NUM_CHANNELS; i++) _lastCC[i] = -1;
        _pendingCount = 0;
    }

private:
    uint8_t _midiChannel;
    int8_t _lastCC[WS_NUM_CHANNELS];
    MidiCCMessage _pending[WS_NUM_CHANNELS];
    int _pendingCount;
};

#endif // WORKSHOP_MIDI_CC_H
