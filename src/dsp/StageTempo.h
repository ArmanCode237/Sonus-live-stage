#pragma once
// SONUS master tempo: an index is persisted, not an absolute millisecond delay.
// Audio-thread safe (no allocations or locks), all divisions relative to quarter-note beats.
#include <algorithm>
namespace NordDSP {
constexpr int TEMPO_DIVISION_COUNT=16;
inline float noteBeats(int index) noexcept {
    constexpr float beats[TEMPO_DIVISION_COUNT]={4.f,6.f,3.f,2.f,4.f/3.f,1.f,1.5f,0.5f,
        0.75f,0.25f,0.375f,0.125f,0.1875f,2.f/3.f,1.f/3.f,1.f/6.f};
    return beats[std::clamp(index,0,TEMPO_DIVISION_COUNT-1)];
}
inline float tempoMilliseconds(float bpm, int division) noexcept {
    return 60000.f/std::clamp(bpm,30.f,300.f)*noteBeats(division);
}
}
