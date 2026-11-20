#include "app/MidiInput.h"
#include "core/WorkstationHost.h"

using namespace SonusDSP;

void dispatchMidiMessage(WorkstationHost& host, const unsigned char* data, std::size_t size, int source) {
    if (!data || size == 0) return;
    // v32.4 real-time messages (one byte): MIDI Clock and START.
    const unsigned char first = data[0];
    if (first == 0xF8) { host.midiClockTick(); return; }
    if (first == 0xFA) { host.midiClockStart(); return; }
    if (first >= 0xF0 || size < 2) return;          // SysEx / system common / running status: ignored

    const unsigned char status = first & 0xF0;
    const unsigned char data1 = data[1] & 0x7F;
    const unsigned char data2 = size > 2 ? (data[2] & 0x7F) : 0;
    const int channel = (first & 0x0F) + 1;
    auto& mappings = host.getMappings();

    if (status == 0x90 && data2 > 0) {
        // A note mapped to a control (pads, buttons) does not play.
        if (mappings.handleMidi(MidiKind::Note, channel, data1, data2, source)) return;
        host.noteOn(data1, data2, source, channel);
    } else if (status == 0x80 || (status == 0x90 && data2 == 0)) {
        if (mappings.handleMidi(MidiKind::Note, channel, data1, 0, source)) return;
        host.noteOff(data1, source, channel);
    } else if (status == 0xC0) {
        // v33 (M1): Program Change only on the chosen channel (default omni), or never.
        // Program n -> slot n mod 8, applied off the MIDI thread by the program worker.
        const int pcChannel = host.midiProgramChannel();
        if (pcChannel < 0 || (pcChannel > 0 && pcChannel != channel)) return;
        host.requestProgramChange(data1);
    } else if (status == 0xD0) {
        host.setAftertouch(data1 / 127.f);              // channel pressure: synth mod source
    } else if (status == 0xE0 && size >= 3) {
        const int bend14 = (static_cast<int>(data2) << 7) | static_cast<int>(data1);
        const float centered = bend14 >= 8192 ? (bend14 - 8192) / 8191.f : (bend14 - 8192) / 8192.f;
        host.setPitchBend(centered);                    // ±2 semitones, center = 8192
    } else if (status == 0xB0 && size >= 3) {
        // The wheel always drives the modules' own CC1 routing; mappings may add targets.
        if (data1 == 1) host.setModWheel(data2 / 127.f);
        if (mappings.handleMidi(MidiKind::CC, channel, data1, data2, source)) return;
        // MIDI-standard messages only. Former implicit CC 7/10/74/71 controls are explicit,
        // visible SYSTEM mappings (see StageApp::loadControlMappings).
        if (data1 == 121) { host.setModWheel(0.f); host.setPitchBend(0.f); return; }
        if (data1 == 64) host.setSustainPedal(data2 >= 64, source, channel);
        else if (data1 == 120 || data1 == 123) {
            // v33 (M1): a controller that sends "all notes off" when it connects or changes
            // mode must not cut the show; the option is on by default (MIDI standard).
            if (host.midiAllNotesOffEnabled()) host.allNotesOff();
        }
    }
}
