#include "app/AudioCallbacks.h"
#include "app/AppCommon.h"
#include "app/MidiInput.h"
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
#elif defined(__APPLE__) && defined(__aarch64__)
    // ARM64 FPCR.FZ (bit 24): flush subnormal floating-point results to zero.
    // The audio callback has its own thread; set every call in case the driver
    // recreates or migrates its callback thread (same intent as SSE FTZ/DAZ).
    unsigned long long fpcr = 0;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24);
    __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
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
    if (!message || message->empty() || !userData) return;
    // v33: userData is the port's MidiSourceTag (stable for the port's lifetime).
    const auto* tag = static_cast<const MidiSourceTag*>(userData);
    if (tag->host) dispatchMidiMessage(*tag->host, message->data(), message->size(), tag->source);
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
