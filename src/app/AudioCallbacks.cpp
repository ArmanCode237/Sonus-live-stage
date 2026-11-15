#include "app/AudioCallbacks.h"
#include "app/AppCommon.h"
#include "core/WorkstationHost.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
#include <xmmintrin.h>
#include <pmmintrin.h>
#define STAGE_HAS_SSE_FTZ 1
#endif

int audioCallback(void* outputBuffer, void* /*inputBuffer*/, unsigned int nBufferFrames,
                  double /*streamTime*/, RtAudioStreamStatus status, void* userData) {
    auto* engine=static_cast<WorkstationHost*>(userData);
    auto* out=static_cast<float*>(outputBuffer);
#ifdef STAGE_HAS_SSE_FTZ
    // Flush denormals to zero on the audio thread (decaying reverb, echo and
    // filter states otherwise sit in the slow denormal range for minutes).
    // Set every callback: some drivers recreate or reuse threads.
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
    const auto begin=std::chrono::steady_clock::now();
    engine->render(out,nBufferFrames);
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-begin).count();
    // Never use console/filesystem/locks from ASIO/WASAPI callback.
    engine->recordAudioCallback(static_cast<uint64_t>(std::max<int64_t>(0,elapsed)),
        nBufferFrames,(status & RTAUDIO_OUTPUT_UNDERFLOW)!=0);
    return 0;
}

void midiCallback(double /*deltatime*/, std::vector<unsigned char>* message, void* userData) {
    if (!message || message->size() < 2) return;
    auto* host = static_cast<WorkstationHost*>(userData);

    unsigned char status = (*message)[0] & 0xF0;
    unsigned char data1  = (*message)[1] & 0x7F;
    unsigned char data2  = (message->size() > 2) ? ((*message)[2] & 0x7F) : 0;

    const int channel = ((*message)[0] & 0x0F) + 1;
    auto& mappings = host->getMappings();
    if (status == 0x90 && data2 > 0) {
        // A note mapped to a control (pads, buttons) does not play.
        if (mappings.handleMidi(MidiKind::Note, channel, data1, data2)) return;
        host->noteOn(data1, data2);
    } else if (status == 0x80 || (status == 0x90 && data2 == 0)) {
        if (mappings.handleMidi(MidiKind::Note, channel, data1, 0)) return;
        host->noteOff(data1);
    } else if (status == 0xD0) {
        host->setAftertouch(data1 / 127.f);              // channel pressure: synth mod source
    } else if (status == 0xE0 && message->size() >= 3) {
        const int bend14 = (static_cast<int>(data2) << 7) | static_cast<int>(data1);
        const float centered = bend14 >= 8192 ? (bend14 - 8192) / 8191.f : (bend14 - 8192) / 8192.f;
        host->setPitchBend(centered); // ±2 semitones, center=8192
    } else if (status == 0xB0) {
        // The wheel always drives the modules' own CC1 routing; mappings may add targets.
        if (data1 == 1) host->setModWheel(data2 / 127.f);
        if (mappings.handleMidi(MidiKind::CC, channel, data1, data2)) return;
        // MIDI-standard messages only. Former implicit CC 7/10/74/71 controls are now
        // explicit, visible SYSTEM mappings (see StageApp::loadControlMappings).
        if (data1 == 121) { host->setModWheel(0.f); host->setPitchBend(0.f); return; }
        if (data1 == 64) host->setSustainPedal(data2 >= 64);
        else if (data1 == 120 || data1 == 123) host->allNotesOff();
    }
}

std::string normalizeMidiPortName(const std::string& rawName) {
    std::string s = rawName;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    auto lastSpace = s.find_last_of(' ');
    if (lastSpace != std::string::npos && lastSpace + 1 < s.size()) {
        bool allDigits = true;
        for (size_t i = lastSpace + 1; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) { allDigits = false; break; }
        }
        if (allDigits) {
            s = s.substr(0, lastSpace);
        }
    }
    return s;
}
