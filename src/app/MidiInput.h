#pragma once
// v33: one interpretation of incoming MIDI for every host (RtMidi ports in the app,
// the DAW's MIDI buffer in the plug-in). Lock-free and allocation-free: it runs on the
// MIDI thread (app) or the audio thread (plug-in).
#include <cstddef>

namespace SonusDSP { class WorkstationHost; }

/** Source ids: 0 = internal / plug-in host, 1..15 = MIDI input ports (stable per port name). */
constexpr int kMidiSourceInternal = 0;
constexpr int kMidiMaxSources = 16;

/** Handles one MIDI message (status byte first). `source` tags notes for MIDI routing. */
void dispatchMidiMessage(SonusDSP::WorkstationHost& host, const unsigned char* data, std::size_t size, int source);
