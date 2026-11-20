#pragma once
#include <RtAudio.h>
#include <string>
#include <vector>

// RtAudio render callback: userData is the WorkstationHost. Never locks or logs.
int audioCallback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
                  double streamTime, RtAudioStreamStatus status, void* userData);
namespace SonusDSP { class WorkstationHost; }
/** v33: what each MIDI input port passes to its callback (who sent the message). */
struct MidiSourceTag { SonusDSP::WorkstationHost* host = nullptr; int source = 0; };
// RtMidi input callback: userData is the port's MidiSourceTag.
void midiCallback(double deltatime, std::vector<unsigned char>* message, void* userData);
// Strips the trailing port index Windows appends to MIDI device names.
std::string normalizeMidiPortName(const std::string& rawName);
