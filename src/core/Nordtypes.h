#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace NordDSP {

constexpr double NSP_SAMPLE_RATE = 44100.0;
constexpr double NSMP_SAMPLE_RATE = 35002.0;
constexpr double NSP_ENVELOPE_RATE = 22050.0;
constexpr double NSMP_ENVELOPE_RATE = 21700.0;
constexpr int NSP_ROOT_OFFSET = 4;

struct LoopInfo {
    bool hasLoop = false;
    bool isAutoLoop = false;
    uint32_t start = 0;
    uint32_t length = 0;
    uint32_t end = 0;
};

struct StrokeSample {
    int index = 0;
    int zone = 0;
    int layerIndex = 0;
    int totalLayersInZone = 1;
    int velocityFrom = 0;
    int switchPoint = 0;
    uint8_t flags = 0;
    bool isReleaseStroke = false;

    int storedRoot = 60;
    int actualRootMidi = 64;
    int lokey = 0;
    int hikey = 127;
    int lovel = 1;
    int hivel = 127;

    uint32_t frames = 0;
    uint32_t startFrameOffset = 0;
    uint32_t sampleRate = 44100;
    LoopInfo loop;

    float fineTuneCents = 0.0f;
    float stretchCents = 0.0f;
    int baseLevelWord = 125;
    float levelGainLinear = 1.0f;
    float loopDecayDbPerS = -3.0f;
    float decayCoef = 0.999985f;
    int damperIndex = 44;
    int softDamperIndex = 50;
    float damperT60Seconds = 0.25f;
    float softDamperT60Seconds = 0.50f;

    std::vector<float> audioL;
    std::vector<float> audioR;
};

struct ClaviaHeaderInfo {
    bool isValidCbin = false;
    std::string signature;
    std::string formatCategory;
    std::string versionStr;
    uint32_t versionRaw = 0;
    uint8_t pianoTypeByte = 5;
    std::string displayName;
    std::string variant;
};

struct EqBandParams {
    float freqHz = 1000.0f;
    float q = 1.0f;
    float gainDb = 0.0f;
};

// 29 parámetros originales + extensiones analógicas compatibles con presets antiguos
struct SynthLayerConfig {
    bool enabled = false;
    int waveType = 0;           // 0=Saw, 1=Square, 2=Triangle, 3=Sine, 4=SuperSaw, 5=Noise, 6=Wavetable, 7=Sample
    float volume = 0.75f;
    float pan = 0.0f;
    int octave = 0;             // -2 a +2
    int semitones = 0;          // -12 a +12
    float detuneCents = 0.0f;
    float cutoffHz = 8500.0f;
    float resonanceQ = 0.85f;
    float attackSec = 0.010f;
    float decaySec = 1.200f;
    float sustainLevel = 0.80f;
    float releaseSec = 0.450f;

    // Parámetros avanzados (Unison, Oscilador, Wavetable y Envolvente de Filtro)
    int unisonVoices = 1;       // 1 a 8 voces por nota
    float unisonDetune = 10.0f; // 0 a 50 cents
    float unisonSpread = 0.50f; // 0.0 a 1.0 (apertura estéreo)
    float phaseOffset = 0.0f;   // 0.0 a 1.0
    float keyTrack = 1.0f;      // 0.0 a 2.0
    float velToCutoff = 0.20f;  // 0.0 a 1.0
    float velToAmp = 1.0f;      // 0.0 a 2.0
    int wavetableIdx = 0;       // 0 a 5
    std::string samplePath = "";
    int filterType = 0;         // 0=LP, 1=HP, 2=BP
    float filterEnvAmt = 0.0f;  // -1.0 a +1.0
    float filterKeyTrack = 0.5f;
    float filtAttackSec = 0.01f;
    float filtDecaySec = 0.40f;
    float filtSustainLvl = 0.50f;
    float filtReleaseSec = 0.40f;

    // DCO analog-style voice additions; defaults keep legacy patches sounding familiar.
    float sawLevel = 0.85f;       // DCO saw oscillator 0..1
    float pulseLevel = 0.40f;     // DCO pulse oscillator 0..1
    float pulseWidth = 0.50f;     // Pulse duty 0.05..0.95
    float pwmDepth = 0.00f;       // LFO-to-pulse-width 0..0.45
    float pwmRateHz = 0.55f;      // PWM LFO 0.05..12 Hz
    float subLevel = 0.25f;       // Square oscillator, -1 octave
    float noiseLevel = 0.02f;     // White noise mix
    float osc2Level = 0.0f;       // Jupiter-inspired second DCO
    float osc2Detune = 7.0f;     // cents
    int osc2Semitones = 0;       // -12..12
    int osc2Wave = 0;            // 0=saw,1=pulse,2=triangle
    float highpassHz = 20.0f;    // Pre-VCF highpass
    float drive = 0.0f;          // Mild VCF pre-drive 0..1
};

// Serializable modulation snapshots (runtime phase intentionally not persisted).
struct SynthLfoPatch {
    bool enabled = false;
    int waveType = 0;
    float rateHz = 2.0f;
    float depth = 0.5f;
    float delaySec = 0.0f;
    float fadeSec = 0.0f;
    int destPitch = 0;
    int destCutoff = 0;
    float destVolume = 0.0f;
    float destPan = 0.0f;
    float destPhase = 0.0f;
};

struct SynthEnvelopePatch {
    float delaySec = 0.0f;
    float attackSec = 0.01f;
    float holdSec = 0.0f;
    float decaySec = 0.2f;
    float sustainLevel = 0.8f;
    float releaseSec = 0.3f;
};

struct SynthPresetParams {
    int maxVoices = 128;
    int transposeSemis = 0;
    int octaveShift = 0;
    float masterTuneCents = 0.0f;
    bool softRelease = false;
    int kbdTouch = 1;

    // Módulo 1: Piano / Sample Principal
    bool pianoEnabled = true;
    float cutoffHz = 20000.0f;
    float resonanceQ = 0.707f;
    float attackSec = 0.001f;
    float decaySec = 3.500f;
    float sustainLevel = 1.00f;
    float releaseSec = 0.350f;
    float loopHold = 0.65f;
    float volume = 0.85f;
    float pan = 0.0f;

    // Módulo 2: Sintetizador de 3 Capas
    bool synthEnabled = false;
    int synthPresetId = 1;
    float synthMasterVol = 0.80f;
    float synthMacroCutoff = 1.0f;
    SynthLayerConfig synthLayers[3];
    SynthLfoPatch synthLfos[4];
    SynthEnvelopePatch synthEnvelopes[3];
    // Persist main analog LFO with the eight GLOBAL programs (not just synth presets).
    bool synthLfoEnabled = false;
    int synthLfoWave = 0;
    float synthLfoRate = 4.5f;
    float synthLfoDepth = 0.0f;
    int synthLfoPitch = 0;       // cents at full depth
    int synthLfoCutoff = 0;      // Hz at full depth

    // Módulo 3: Órgano Nord C2D (B3 / Vox / Farfisa / Pipe)
    bool organEnabled = false;
    int organModel = 0;         // 0=B3, 1=Vox, 2=Farfisa, 3=Pipe
    float organVolume = 0.80f;
    bool organRotaryFast = false;
    float organDrive = 0.12f;
    float drawbars[9] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; // 88 8000 000
    int organVibratoMode = 0;   // 0=Off, 1=V1, 2=V2, 3=V3, 4=C1, 5=C2, 6=C3
    bool organPercEnabled = false;
    int organPercHarmonic = 3;  // 2=2nd, 3=3rd
    float organPercDecaySec = 0.35f;
    float organPercLevel = 0.85f;

    // Ecualizador Paramétrico de 5 Bandas
    bool eqEnabled = true;
    float lowCutHz = 20.0f;
    float highCutHz = 20000.0f;
    EqBandParams eqBands[5] = {
        {60.0f,    0.9f, 0.0f},
        {250.0f,   1.0f, 0.0f},
        {1000.0f,  1.0f, 0.0f},
        {4000.0f,  1.0f, 0.0f},
        {12000.0f, 0.9f, 0.0f}
    };

    // Modulaciones y Delay
    bool chorusEnabled = false;
    float chorusRateHz = 0.8f;
    float chorusDepth = 0.45f;
    float chorusMix = 0.35f;

    bool phaserEnabled = false;
    float phaserRateHz = 0.6f;
    float phaserDepth = 0.60f;
    float phaserFeedback = 0.40f;
    float phaserMix = 0.35f;

    bool tremoloEnabled = false;
    float tremoloRateHz = 4.5f;
    float tremoloDepth = 0.50f;

    bool delayEnabled = false;
    float delayTimeMs = 360.0f;
    float delayFeedback = 0.35f;
    float delayDamping = 0.35f;
    float delayMix = 0.25f;
    bool delayPingPong = true;

    // Reverb y Shimmer
    bool reverbEnabled = true;
    int reverbType = 2;
    float reverbMix = 0.25f;
    float reverbSize = 0.65f;
    float reverbDamping = 0.40f;
    float reverbPreDelayMs = 18.0f;
    float reverbShimmer = 0.45f;

    float shimmerShiftSt = 12.0f;
    float shimmerFeedback = 0.65f;
    float shimmerDiffusion = 0.88f;
    float shimmerLowCutHz = 140.0f;
    float shimmerHighCutHz = 8500.0f;
    float shimmerModRateHz = 0.45f;
    float shimmerModDepth = 0.25f;
    int shimmerPitchMode = 1;
    int shimmerReverbMode = 2;
    int shimmerColorMode = 1;
};

struct PianoInstrument {
    uint64_t id = 0;
    std::string name = "No Sound Loaded";
    std::string variant;
    std::string version = "6.00";
    std::string formatType = "NONE";
    std::string sourcePath;
    bool isV6 = true;
    int channels = 2;
    int zoneCount = 0;
    double memoryMB = 0.0;
    int releaseMode = 0;
    uint8_t undampedFromKey = 90;
    ClaviaHeaderInfo headerInfo;
    SynthPresetParams defaultPreset;
    std::vector<StrokeSample> strokes;
};

struct LibraryEntry {
    std::string name;
    std::string path;
    std::string extension;
    std::string category;
    std::string instrumentGroup;
    uintmax_t sizeBytes = 0;
    bool hasAudioReady = true;
};

} // namespace NordDSP