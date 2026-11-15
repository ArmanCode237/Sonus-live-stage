#pragma once
#include <RtAudio.h>
#include <string>
#include <vector>

// RtAudio render callback: userData is the WorkstationHost. Never locks or logs.
int audioCallback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
                  double streamTime, RtAudioStreamStatus status, void* userData);
// RtMidi input callback: userData is the WorkstationHost.
void midiCallback(double deltatime, std::vector<unsigned char>* message, void* userData);
// Strips the trailing port index Windows appends to MIDI device names.
std::string normalizeMidiPortName(const std::string& rawName);
