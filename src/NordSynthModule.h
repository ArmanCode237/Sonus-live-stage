#pragma once
#include <vector>
#include <array>
#include <atomic>
#include <cmath>
#include <string>

namespace NordDSP {

enum class SynthWaveform { Saw = 0, Square, Triangle, Sine, Noise };

struct SynthLayerParams {
    bool enabled = false;
    std::string name = "Layer";
    SynthWaveform waveform = SynthWaveform::Saw;
    float volume = 0.8f;
    float pan = 0.0f;           // -1.0 a +1.0
    int octaveTranspose = 0;    // -2 a +2 octavas
    int semiTranspose = 0;      // -12 a +12 semitonos
    float detuneCents = 0.0f;   // -50 a +50 centésimas

    // Envolvente ADSR
    float attackSec = 0.005f;
    float decaySec = 1.200f;
    float sustainLevel = 0.75f;
    float releaseSec = 0.400f;

    // Filtro por Capa
    float cutoffHz = 12000.0f;
    float resonanceQ = 0.707f;
};

struct SynthVoice {
    bool active = false;
    int midiKey = -1;
    double phase = 0.0;
    double phaseInc = 0.0;
    float envLevel = 0.0f;
    int envStage = 0; // 0=Idle, 1=Attack, 2=Decay, 3=Sustain, 4=Release
    float velocity = 1.0f;
};

class SynthLayerEngine {
public:
    SynthLayerParams params;
    std::array<SynthVoice, 24> voices;

    void noteOn(int midiKey, int velocity, double sampleRate);
    void noteOff(int midiKey);
    void allNotesOff();
    void render(float* outStereoInterleaved, unsigned int numFrames, double sampleRate);
};

class NordSynthModule {
public:
    NordSynthModule();
    void setSampleRate(double sr);

    void noteOn(int midiKey, int velocity);
    void noteOff(int midiKey);
    void allNotesOff();

    // 3 Capas independientes del Sintetizador
    std::array<SynthLayerEngine, 3> layers;
    std::atomic<float> masterVolume_{0.85f};
    std::atomic<int> activePresetId_{1};

    void render(float* outInterleavedStereo, unsigned int numFrames);

private:
    double hostSampleRate_ = 48000.0;
};

} // namespace NordDSP