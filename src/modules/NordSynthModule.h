#pragma once
#include "../dsp/NordDspEffects.h"
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>

namespace NordDSP {

constexpr int SYNTH_MAX_VOICES_PER_LAYER = 32;
constexpr int SYNTH_NUM_LFOS = 4;
constexpr int SYNTH_NUM_ENVS = 3;
constexpr int SYNTH_WAVETABLE_SIZE = 256;
constexpr int SYNTH_MAX_UNISON = 8;

struct Wavetable {
    std::string name = "Sine";
    std::array<float, SYNTH_WAVETABLE_SIZE> data{};
    bool custom = false;
};

struct SynthLFO {
    bool enabled = false;
    int   waveType = 0;          // 0=sine,1=tri,2=saw,3=sqr,4=s&h
    float rateHz = 2.0f;
    float depth = 0.5f;
    float phase = 0.0f;
    float delaySec = 0.0f;
    float fadeSec = 0.0f;
    int   destPitch = 0;
    int   destCutoff = 0;
    float destVolume = 0.0f;
    float destPan = 0.0f;
    float destPhase = 0.0f;
    double phaseAccum = 0.0;
    float currentVal = 0.0f;
    float delayRemain = 0.0f;
    float fadeGain = 0.0f;
};

struct SynthEnvelope {
    float delaySec = 0.0f;
    float attackSec = 0.01f;
    float holdSec = 0.0f;
    float decaySec = 0.2f;
    float sustainLevel = 0.8f;
    float releaseSec = 0.3f;
};

struct SynthPolyVoice {
    bool active = false;
    int  rawKey = 0;
    int  playedKey = 60;
    bool keyHeld = false;
    bool sustained = false;
    int  stage = 0;              // 0=idle,1=atk,2=dec,3=sus,4=rel
    float env = 0.0f;
    float filtEnv = 0.0f;
    float velGain = 1.0f;
    float velNorm = 0.8f;
    float unisonScale = 1.0f;
    float noiseFilterState = 0.0f; // Estado individual por voz (evita distorsión entre voces)
    double freq = 440.0;
    double phase1 = 0.0;
    double phase2 = 0.0;
    double phase3 = 0.0;
    float panOffset = 0.0f;
    float detuneMult = 1.0f;
    float phaseInit = 0.0f;
    int   sampleFrame = 0;
    float sampleLoopStart = 0;
    float sampleLoopEnd = 0;
    std::vector<float>* sampleDataL = nullptr;
    std::vector<float>* sampleDataR = nullptr;
    double sampleStep = 1.0;
};

class SingleSynthLayer {
public:
    SynthLayerConfig cfg;
    std::array<SynthPolyVoice, SYNTH_MAX_VOICES_PER_LAYER> voices{};

    void init(float sr);
    void reset();
    void noteOn(int rawKey, int playedKey, int velocity);
    void noteOff(int rawKey, bool pedalDown);
    void setSustain(bool down);
    void renderLayer(float* mixL, float* mixR, unsigned int numFrames, float sr,
                     float macroCutoff, const std::vector<Wavetable>& wtBank,
                     const SynthLFO* lfos, const SynthEnvelope* envs);

private:
    float sr_ = 48000.0f;
    StateVariableFilter filter;
    float smoothedCutoff_ = 8500.0f;
    float smoothedQ_ = 0.85f;
    const std::vector<Wavetable>* wtBank_ = nullptr;

    inline float polyBLEP(double t, double dt);
    inline float naiveSaw(double phase);
    inline float naiveSquare(double phase);
    float readWavetable(int idx, double phase) const;
};

class NordSynthModule {
public:
    bool  synthEnabled = false;
    int   synthPresetId = 1;
    float synthMasterVol = 0.8f;
    float synthMacroCutoff = 1.0f;
    std::array<SingleSynthLayer, 3> layers;

    std::array<SynthLFO, SYNTH_NUM_LFOS> lfos{};
    std::array<SynthEnvelope, SYNTH_NUM_ENVS> envelopes{};
    std::vector<Wavetable> wavetableBank;
    std::mutex wtMutex;

    void init(float sr);
    void loadBuiltInSynthPreset(int presetId);
    void noteOn(int rawKey, int playedKey, int velocity);
    void noteOff(int rawKey, bool pedalDown);
    void setSustain(bool down);
    void allNotesOff();
    void render(float* outInterleavedStereo, unsigned int numFrames);

    void addWavetable(const Wavetable& wt);
    void clearWavetables();
    void generateDefaultWavetables();
    const std::vector<Wavetable>& getWavetableBank() const { return wavetableBank; }

    void setLFO(int idx, const SynthLFO& lfo);
    void setEnvelope(int idx, const SynthEnvelope& env);
    void updateLFOsAndEnvs(unsigned int numFrames, float sr);

private:
    float sr_ = 48000.0f;
    std::vector<float> bufL_, bufR_;
};

} // namespace NordDSP