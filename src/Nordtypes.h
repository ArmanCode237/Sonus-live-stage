#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace NordDSP {

constexpr double NSP_SAMPLE_RATE = 44100.0;
constexpr double NSP_ENVELOPE_RATE = 22050.0;
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

struct SynthPresetParams {
    int maxVoices = 128;
    int transposeSemis = 0;
    int octaveShift = 0;
    float masterTuneCents = 0.0f;
    bool softRelease = false;
    int kbdTouch = 1;

    // Filtro y Envolvente
    float cutoffHz = 20000.0f;
    float resonanceQ = 0.707f;
    float attackSec = 0.001f;
    float decaySec = 3.500f;
    float sustainLevel = 1.00f;
    float releaseSec = 0.350f;
    float loopHold = 0.65f;
    float volume = 0.85f;
    float pan = 0.0f;

    // Ecualizador Paramétrico de 5 Bandas + HPF/LPF
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

    // Modulaciones (Chorus, Phaser, Tremolo)
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

    // Delay Independiente
    bool delayEnabled = false;
    float delayTimeMs = 360.0f;
    float delayFeedback = 0.35f;
    float delayDamping = 0.35f;
    float delayMix = 0.25f;
    bool delayPingPong = true;

    // Reverb Clásica (0 = Room, 1 = Plate, 2 = Hall, 3 = ShimmerReverbEngine)
    bool reverbEnabled = true;
    int reverbType = 2;
    float reverbMix = 0.28f;
    float reverbSize = 0.65f;
    float reverbDamping = 0.40f;
    float reverbPreDelayMs = 18.0f;
    float reverbShimmer = 0.45f;

    // Parámetros dedicados de ShimmerReverbEngine (ValhallaShimmer Architecture)
    float shimmerShiftSt = 12.0f;       // -12 a +12 semitonos
    float shimmerFeedback = 0.60f;      // 0.0 a 0.985
    float shimmerDiffusion = 0.88f;     // 0.0 a 0.98
    float shimmerLowCutHz = 140.0f;     // 20 a 1000 Hz
    float shimmerHighCutHz = 8500.0f;   // 1000 a 20000 Hz
    float shimmerModRateHz = 0.45f;     // 0.05 a 5.0 Hz
    float shimmerModDepth = 0.22f;      // 0.0 a 1.0
    int shimmerPitchMode = 1;           // 0=Bypass, 1=Single, 2=Dual, 3=SingleReverse, 4=DualReverse
    int shimmerReverbMode = 2;          // 0=Mono, 1=SmallStereo, 2=MediumStereo, 3=BigStereo
    int shimmerColorMode = 1;           // 0=Bright, 1=Dark
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