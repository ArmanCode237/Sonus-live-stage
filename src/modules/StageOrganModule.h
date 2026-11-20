#pragma once
#include "../dsp/StageDspEffects.h"
#include <array>
#include <vector>
#include <string>

namespace SonusDSP {

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
    double accelTau = 0.65; // Horn accelerates faster than heavy bass drum
    double decelTau = 1.05;
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
    float percRamp = 1.f;
    bool useSample = false;
    double samplePos = 0.0;
    double sampleStep = 1.0;
    int    sampleLoopStart = 0;
    int    sampleLoopEnd = 0;
    std::vector<float>* sampleDataL = nullptr;
    std::vector<float>* sampleDataR = nullptr;
};

class StageOrganModule {
public:
    bool  organEnabled = false;
    int   organModel = 0;         // 0 B3, 1 Vox, 2 Farfisa, 3 Pipe, 4 Mellow, 5 Bright, 6 Combo, 7 Theatre Pipe
    float organVolume = 0.80f;
    bool  organRotaryFast = false;
    float organDrive = 0.12f;
    float rotaryMix = 1.0f;
    // Rotary fast mode increases horn/drum rotation (Doppler + tremolo);
    // these are intrinsic cabinet properties, not Studio FX send parameters.
    float rotaryFastRate = 1.0f;    // 0.6 .. 1.5x fast speed
    float rotaryRamp = 1.0f;        // .4 .. 2.5x acceleration/deceleration time
    float rotaryStereoWidth = 1.0f; // 0=mono cabinet, 1=original stereo, 1.5=wide

    float vibratoDepth = 1.0f;
    float vibratoRate = 6.8f;
    bool chorusEnabled = false;
    float chorusRate = 0.65f, chorusDepth = 0.55f, chorusMix = 0.30f;
    float chorusWidth = 0.85f, chorusFeedback = 0.08f, chorusTone = 9000.0f;
    int chorusVoices = 3;
    float pitchBendSemis = 0.0f;
    float modWheel = 0.0f;
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
    int voiceLimit=ORGAN_MAX_VOICES; // Assigned by host on audio callback thread.

    void init(float sr);
    void noteOn(int rawKey, int playedKey, int velocity);
    void noteOff(int rawKey, bool pedalDown);
    void setSustain(bool down);
    void allNotesOff();
    void render(float* outInterleavedStereo, unsigned int numFrames);
    bool loadOrganSample(const std::string& path);
    /** Audio to produce: enabled with level, or still fading out after OFF / volume 0.
        The host skips the organ (no CPU) when this is false. */
    bool needsRender() const { return (organEnabled && organVolume > 0.001f) || volumeSmooth_ > 1e-6f; }
    /** Voices currently allocated (sounding or releasing). */
    int activeVoices() const { int n = 0; for (const auto& v : organVoices) n += v.active ? 1 : 0; return n; }

private:
    float sr_ = 48000.0f;
    ModulationRack ownChorus_;
    LeslieRotor horn_;
    LeslieRotor drum_;
    float drumLP_state_ = 0.0f;
    // Sample-delay acoustic path: two preallocated fractional delay lines,
    // no allocations or locks in the real-time callback.
    static constexpr int ROTOR_DELAY_SIZE = 1024;
    std::array<float, ROTOR_DELAY_SIZE> hornDelay_{};
    std::array<float, ROTOR_DELAY_SIZE> drumDelay_{};
    int rotorWritePos_ = 0;
    std::array<float, VIB_DELAY_SAMPLES> vibDelayLine_{};
    int  vibWritePos_ = 0;
    double vibLfoPhase_ = 0.0;
    double wheelPhase_ = 0.0;
    float clickEnvelope_ = 0.0f;
    float clickSample_ = 0.0f;
    // v32 live smoothing: every control the player moves while keys are held
    // (drawbars, volume / ON-OFF, drive, model) is ramped instead of jumping.
    std::array<float, 9> drawbarSmooth_{};
    float volumeSmooth_ = 0.0f;
    float driveSmooth_ = 0.0f;
    int   currentModel_ = 0;
    int   previousModel_ = 0;
    float modelMix_ = 1.0f;       // 0 → previous model, 1 → current model (25 ms crossfade)
    bool  smoothPrimed_ = false;
    void snapSmoothing();
};

} // namespace SonusDSP