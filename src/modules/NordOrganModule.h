#pragma once
#include "../dsp/NordDspEffects.h"
#include <array>
#include <vector>
#include <string>

namespace NordDSP {

constexpr int ORGAN_MAX_VOICES = 64;
constexpr int VIB_DELAY_SAMPLES = 2048;
constexpr double LESLIE_SLOW_RPS = 0.85;
constexpr double LESLIE_FAST_RPS = 6.7;
constexpr double LESLIE_FAST_DRUM_RPS = 5.8;
constexpr double AMP_MOD_DEPTH = 0.35;

struct LeslieRotor {
    double phase = 0.0;
    double currentRPS = LESLIE_SLOW_RPS;
    double targetRPS = LESLIE_SLOW_RPS;
    double accelTau = 0.35; // Aceleración física realista de bocina Leslie
    double decelTau = 0.55;
};

struct OrganVoice {
    bool active = false;
    int  rawKey = 0;
    bool keyHeld = false;
    bool sustained = false;
    int  stage = 0;         // 0=Idle, 3=Sustain, 4=FastAntiClickRelease
    float env = 0.0f;
    float velGain = 1.0f;
    double freq = 440.0;
    double phase1 = 0.0;    // Se envuelve en 2.0 para que 0.5 (16') y 1.5 (5 1/3') no tengan saltos
    double percEnv = 0.0;
    bool percActive = false;
    bool useSample = false;
    double samplePos = 0.0;
    double sampleStep = 1.0;
    int    sampleLoopStart = 0;
    int    sampleLoopEnd = 0;
    std::vector<float>* sampleDataL = nullptr;
    std::vector<float>* sampleDataR = nullptr;
};

class NordOrganModule {
public:
    bool  organEnabled = false;
    int   organModel = 0;         // 0=B3 Tonewheel, 1=Vox Continental, 2=Farfisa, 3=Pipe Organ
    float organVolume = 0.80f;
    bool  organRotaryFast = false;
    float organDrive = 0.12f;
    // Registro clásico B3 88 8000 000 por defecto (nunca inicia en silencio)
    std::array<float, 9> drawbars = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int   vibratoMode = 0;        // 0=Off, 1=V1, 2=V2, 3=V3, 4=C1, 5=C2, 6=C3
    bool  percussionEnabled = true;
    int   percussionHarmonic = 3; // 2=2nd (4'), 3=3rd (2 2/3')
    float percussionDecaySec = 0.35f;
    float percussionLevel = 0.80f;

    bool  useSamples = false;
    std::string organSamplePath = "";
    std::vector<float> organSampleL;
    std::vector<float> organSampleR;
    int    organSampleRootKey = 60;
    float  organSampleLoopStart = 0;
    float  organSampleLoopEnd = 0;
    bool   organSampleLoop = true;

    std::array<OrganVoice, ORGAN_MAX_VOICES> organVoices{};

    void init(float sr);
    void noteOn(int rawKey, int playedKey, int velocity);
    void noteOff(int rawKey, bool pedalDown);
    void setSustain(bool down);
    void allNotesOff();
    void render(float* outInterleavedStereo, unsigned int numFrames);
    bool loadOrganSample(const std::string& path);

private:
    float sr_ = 48000.0f;
    LeslieRotor horn_;
    LeslieRotor drum_;
    float drumLP_state_ = 0.0f;
    std::array<float, VIB_DELAY_SAMPLES> vibDelayLine_{};
    int  vibWritePos_ = 0;
    double vibLfoPhase_ = 0.0;
    float clickEnvelope_ = 0.0f;
    float clickSample_ = 0.0f;
};

} // namespace NordDSP