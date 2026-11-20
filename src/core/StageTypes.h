#pragma once
#include "../dsp/Dx7Engine.h"
#include <vector>
#include <array>
#include <string>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <atomic>
#include "../dsp/SynthLayerInsertFx.h"

namespace SonusDSP {

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
    // Validated NSMP SecondStart converted from compressed-word offsets to PCM frames.
    uint32_t alternateStartFrame = 0;
    // A one-shot is never eligible for acoustic-piano auto-loop synthesis.
    bool explicitOneShot = false;

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
    // SoundFont sampleModes: 0=no loop, 1=continuous, 3=until note release.
    // Keep non-SF2 instruments unchanged; their loop mode is managed elsewhere.
    uint8_t sf2LoopMode = 0;

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

    // Stable, full-length PCM16 storage: never resize after publishing the bank.
    std::vector<int16_t> pcmL;
    std::vector<int16_t> pcmR;
    float sampleScale = 1.0f / 16384.0f;
    // Producer writes PCM, then publishes the contiguous valid prefix with release.
    std::shared_ptr<std::atomic<uint32_t>> readyFrames = std::make_shared<std::atomic<uint32_t>>(0);
    // An automatically detected loop may become known after progressive decoding.
    // Packed as (start << 32 | end), 0 means no published automatic loop.
    std::shared_ptr<std::atomic<uint64_t>> publishedAutoLoop = std::make_shared<std::atomic<uint64_t>>(0);

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
/** v32.4: fixed-size text, trivially copyable. Layer configs are copied by the audio
 *  thread every block; a std::string here could be freed by another thread mid-copy. */
struct FixedText {
    std::array<char, 512> chars{};
    FixedText() = default;
    FixedText(const std::string& s) { assign(s); }             // NOLINT: implicit on purpose
    FixedText& operator=(const std::string& s) { assign(s); return *this; }
    void assign(const std::string& s) {
        const size_t n = std::min(s.size(), chars.size() - 1);
        std::copy_n(s.data(), n, chars.data()); chars[n] = 0;
    }
    std::string str() const { return std::string(chars.data()); }
    bool empty() const { return chars[0] == 0; }
};

struct SynthLayerConfig {
    bool enabled = false;
    int waveType = 0;           // see SYNTH_WAVE_COUNT: 0=Saw … 28=Tri ensemble, 29=PD saw, 30=PD reso,
                                // 31=Triple VCO, 32=Sync sweep, 33=Hypersaw, 34=Pink noise, 35=8-bit pulse
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
    FixedText samplePath{};
    int filterType = 0;         // 0=LP12, 1=HP12, 2=BP12, 3=Notch, 4=LP24 ladder, 5=LP24 OTA
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
    int synthesisType = 0; // 0=subtractive, 1=FM (2-op), 2=wavetable morph, 3=FM4 (4-op), 4=LA hybrid, 5=DX7 (msfa), 6=MT-32 (libmt32emu), 7=LA32 synth partials
    // v32.2 DX7 layer (synthesisType 5): a complete DX7 voice (VCED + operator mask).
    // dx7Revision changes whenever a new voice is assigned (the audio thread reloads it).
    Dx7Voice dx7Voice = dx7InitVoice();
    uint32_t dx7Revision = 0;
    // v32.2 MT-32 layer (synthesisType 6): timbre 0-127 (group A 1-64, group B 1-64).
    int mt32Program = 0;
    float fmIndex = 2.0f;
    float fmRatio = 2.0f;
    float wavetableMorph = 0.0f;
    // CC1 modulation destinations (per layer, additive to global routes).
    float wheelCutoffOctaves = 0.f; // -6..+6 octaves, reversible
    float wheelAmpDb = 0.f;         // -24..+12 dB
    float wheelVibratoCents = 30.f; // 0..200 cents, preserving historical behavior
    float wheelVibratoRateHz = 5.2f;
    float wheelWtMorph = 0.f;       // -1..+1 from current wavetable position
    int wheelMode = 0;             // 0=vibrato,1=custom routes,2=vibrato+custom,3=off (per layer)
    int voiceMode = 0;
    float portamento = 0.0f;
    bool portamentoEnabled = false;
    int glideMode = 1;
    float slide = 0.0f;
    int keyLow = 0, keyHigh = 127, keyTranspose = 0;
    bool seqEnabled=false;
    float seqBpm=120.f;
    int seqSteps=16;
    float seqGate=.8f;
    std::array<int,16> seqNotes{}; // -24..24 semitones, 99 = rest
    // v32.3: 303-style accent / slide per step, rate division, tempo follow.
    std::array<uint8_t,16> seqAccent{}, seqSlide{};
    int seqDivision = 3;           // 0 1/4, 1 1/8, 2 1/8T, 3 1/16, 4 1/16T, 5 1/32
    bool seqSync = false;          // follow the master tempo instead of seqBpm
    // v32.4: output trim after the insert rack (LEVEL still drives filter / inserts).
    float outGainDb = 0.f;         // -24 .. +24 dB
    // v32.4 layer EQ (first insert): high / low pass 12-24-48 dB/oct and three peaking bands.
    bool eqEnabled = false;
    float eqHighPass = 20.f, eqLowPass = 20000.f;
    int eqHighPassSlope = 12, eqLowPassSlope = 12;
    std::array<float,3> eqFreq{{160.f,1100.f,6500.f}};
    std::array<float,3> eqQ{{0.8f,1.f,0.8f}};
    std::array<float,3> eqGain{{0.f,0.f,0.f}};
    // Five independent per-layer insert processors, stored with the patch.
    InsertFxChain insertFx{};

    // v31 voice character: per-voice pitch drift and filter spread (0 = perfectly stable).
    float analog = 0.0f;
    // v31 FM4: four sine operators (index 0 = OP1). Each has its own envelope; the
    // algorithm decides which operators are carriers. Feedback acts on OP4.
    int fm4Algorithm = 0;                          // 0..7, see FM4_ALGORITHMS
    float fm4Feedback = 0.0f;                      // 0..1
    float fm4VelMod = 0.5f;                        // velocity → modulator depth 0..1
    std::array<float,4> opRatio{{1.f, 1.f, 2.f, 1.f}};   // 0.5..16 × note frequency
    std::array<float,4> opLevel{{1.f, .5f, .5f, .3f}};   // carrier gain / modulator depth
    std::array<float,4> opAttack{{.002f, .002f, .002f, .002f}};
    std::array<float,4> opDecay{{1.5f, .8f, .8f, .8f}};
    std::array<float,4> opSustain{{.6f, .3f, .3f, .3f}};
    // v31 LA hybrid: wavetable partial + DCO partial, optional ring modulation.
    float laMix = 0.5f;                            // 0 = wavetable partial, 1 = DCO partial
    float laRing = 0.0f;                           // 0..1 ring-modulated share
    // v31 attack transient (any synthesis type): procedural PCM-style attack partial.
    int transientType = 0;                         // 0..7, see SYNTH_TRANSIENTS
    float transientLevel = 0.0f;                   // 0 = off
    float transientDecay = 0.08f;                  // seconds
    float transientTone = 0.5f;                    // brightness 0..1
};

constexpr int SYNTH_WAVE_COUNT = 36;
constexpr int SYNTH_FILTER_TYPE_COUNT = 6;
constexpr int SYNTH_SYNTHESIS_TYPE_COUNT = 8;
constexpr int SYNTH_LFO_WAVE_COUNT = 6;   // sine, tri, saw, square, S&H, smooth random
constexpr int SYNTH_TRANSIENT_COUNT = 8;
// v32.3: 96 designs (1-96) + 48 vintage (97-144) + 24 DX7 voices (145-168) + 16 LA32 (169-184).
constexpr int SYNTH_FACTORY_PRESET_COUNT = 184;

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
    bool keySync = false;
};

struct SynthEnvelopePatch {
    float delaySec = 0.0f;
    float attackSec = 0.01f;
    float holdSec = 0.0f;
    float decaySec = 0.2f;
    float sustainLevel = 0.8f;
    float releaseSec = 0.3f;
};

constexpr int PIANO_LAYER_COUNT = 5;
constexpr int SYNTH_LAYER_COUNT = 4;
constexpr int FX_SOURCE_COUNT = PIANO_LAYER_COUNT + SYNTH_LAYER_COUNT + 1; // 5 piano + 4 synth + organ
constexpr int FX_BUS_COUNT = 3; // Modulation, Delay, Reverb

// V7.2 stereo routing: local PIANO/SYNTH/ORGAN racks followed by GLOBAL.
// EQ is a movable stage at any of six insert boundaries; five FX slots are independent.
constexpr int STUDIO_RACK_COUNT=4;
constexpr int STUDIO_SLOTS=5;
constexpr int STUDIO_MAX_SOURCES=5;
constexpr int STUDIO_PARAM_COUNT=24; // v28.2.5: extra pitch layers / tone controls
struct StudioSlotConfig {
    int version=1; // 1 = original effects; 2 = extended parameters, opt-in to preserve legacy presets
    int type=0; // 0 off; 1 chorus;2 phaser;3 tremolo;4 flanger;5 delay;6 reverb;
                // 7 drive;8 autopan;9 compressor;10 bitcrusher;11 stereo width;12 parametric EQ
    bool enabled=false;
    // p0-p5 preserve the V7.2 layout. p6-p15 provide advanced Shimmer control.
    // Values remain normalized 0..1, so old JSON patches deserialize safely.
    std::array<float,STUDIO_PARAM_COUNT> p{{0.45f,0.55f,0.25f,0.45f,0.7f,0.25f,
                             1.f,0.5f,0.25f,0.48f,0.30f,0.15f,0.5f,1.f,1.f,0.8f,
                             // p16..23 are opt-in Shimmer v3 parameters.
                             .6458333f,.30f,.40f,.50f,.70f,.50f,.65f,0.f}};
};
struct StudioRackConfig {
    bool eqEnabled=true;
    std::array<float,5> eqDb{{0.f,0.f,0.f,0.f,0.f}};
    std::array<float,5> eqFreq{{60.f,250.f,1000.f,4000.f,12000.f}};
    std::array<float,5> eqQ{{.9f,1.f,1.f,1.f,.9f}};
    float eqHighPass=20.f,eqLowPass=20000.f;
    int eqHighPassSlope=12,eqLowPassSlope=12; // dB/oct, 12/24/48
    float eqInputDb=0.f,eqOutputDb=0.f;
    int eqPosition=0; // 0=pre inserts, 1..5=after slot 1..5

    std::array<StudioSlotConfig,STUDIO_SLOTS> slots{};
    std::array<std::array<float,STUDIO_SLOTS>,STUDIO_MAX_SOURCES> sends{};
};

struct PianoLayerSampleConfig {
    // Post-voice, pre-insert Sound trim. Does not change sample normalization.
    float gainDb = 0.0f; // -36..+18 dB, default unity
    float attackSec = 0.005f;
    float decaySec = 3.5f;
    float sustainLevel = 1.0f;
    float releaseSec = 0.35f;
    float loopHold = 0.65f;
    float pan = 0.0f;
    float fineTuneCents = 0.0f;
    float cutoffHz = 20000.0f;
    float resonanceQ = 0.707f;
    float wheelCutoffOctaves = 0.f; // -6..+6, independent per sample layer
    float wheelAmpDb = 0.f;         // -24..+12 dB
    // v28.2.2: module switches are independent of depth. These defaults keep
    // freshly imported samples free of unintended pitch or amplitude changes.
    bool wheelCutoffEnabled = false;
    bool wheelCutoffInvert = false; // CC1=0 -> cutoff at minimum, CC1=1 -> configured cutoff
    bool wheelAmpEnabled = false;
    bool wheelVibratoEnabled = false;
    float wheelVibratoCents = 35.f;
    float wheelVibratoRateHz = 5.2f;
    // Non-destructive per-Sound parametric EQ. Lives in the individual sample
    // layer and is saved with a Sound or its Performance snapshot.
    bool soundEqEnabled = false;
    std::array<float,3> soundEqFreq{{160.f,1100.f,6500.f}};
    std::array<float,3> soundEqQ{{0.8f,1.f,0.8f}};
    std::array<float,3> soundEqGain{{0.f,0.f,0.f}};
    float soundEqHighPass=20.f,soundEqLowPass=20000.f;
    int soundEqHighPassSlope=12,soundEqLowPassSlope=12;
    bool monoMode = false;
    bool legatoEnabled = false;    // independent of portamento
    bool portamentoEnabled = false;
    float portamentoSec = 0.12f;
    int portamentoMode = 1; // 0=always, 1=only overlapping notes
    // v31: the Sound's own insert chain (chorus, phaser, echo, reverb); off by default.
    InsertFxChain insertFx{};
};

struct KeyZone {
    int low = 0;
    int high = 127;
    int transpose = 0;   // semitones; piano layers: -12..12 (v32.5)
    int octave = 0;      // piano layers: -2..2 (v32.5)
    bool contains(int midi) const { return midi >= low && midi <= high; }
};

struct ModuleFxConfig {
    bool chorusEnabled = false;
    float chorusRate = 0.7f;
    float chorusDepth = 0.55f;
    float chorusMix = 0.35f;
    bool delayEnabled = false;
    float delayMs = 330.0f;
    float delayFeedback = 0.28f;
    float delayMix = 0.28f;
    bool reverbEnabled = false;
    int reverbType = 2;
    float reverbMix = 0.30f;
    float reverbSize = 0.70f;
    bool driveEnabled = false;
    float driveAmount = 0.25f;
    bool autoPanEnabled = false;
    float autoPanRate = 1.5f;
    float autoPanDepth = 0.45f;
    bool phaserEnabled = false;
    float phaserRate = 0.65f, phaserDepth = 0.60f, phaserFeedback = 0.35f, phaserMix = 0.40f;
    bool tremoloEnabled = false;
    float tremoloRate = 4.5f, tremoloDepth = 0.50f;
    bool flangerEnabled = false;
    float flangerRate = 0.35f, flangerDepth = 0.55f, flangerFeedback = 0.25f, flangerMix = 0.35f;
};

struct FxSendLevels {
    float modulation = 1.0f;
    float delay = 1.0f;
    float reverb = 1.0f;
};

/** v32.3 modulation connection as stored in presets (target = registry id, e.g. "L2_cut").
    See modules/SynthModMatrix.h. */
struct ModConnectionSpec {
    int source = 0;
    std::string target;
    float amount = .25f;     // normalized, -1..1
    bool bipolar = false;
    int curve = 0;           // 0 linear, 1 exponential, 2 logarithmic, 3 S
};

struct SynthPresetParams {
    float masterBpm = 120.0f;
    bool delayTempoSync = false;
    int delayDivision = 5;
    bool studioRoutingEnabled=true;
    std::array<StudioRackConfig,STUDIO_RACK_COUNT> studioRacks{};
    int maxVoices = 128;
    int transposeSemis = 0;
    int octaveShift = 0;
    float masterTuneCents = 0.0f;
    bool softRelease = false;
    int kbdTouch = 1;

    // Módulo 1: Piano / Sample Principal
    bool pianoEnabled = true;
    // Five sample layers. Older 3-layer programs load into L1-L3; L4-L5 remain off.
    float pianoVolume = 1.0f;
    float pianoPan = 0.f, pianoTune = 0.f; // module local stereo and cents
    bool pianoLayerEnabled[PIANO_LAYER_COUNT] = {true, false, false, false, false};
    float pianoLayerVolume[PIANO_LAYER_COUNT] = {1.0f, 0.75f, 0.75f, 0.75f, 0.75f};
    PianoLayerSampleConfig pianoLayerSample[PIANO_LAYER_COUNT]{};
    FxSendLevels fxSends[FX_SOURCE_COUNT]{};
    KeyZone pianoZone{};
    KeyZone synthZone{};
    KeyZone organZone{};
    KeyZone pianoLayerZones[PIANO_LAYER_COUNT]{};
    bool synthVoiceMaster = true;
    FxSendLevels synthFxSends[SYNTH_LAYER_COUNT]{};
    FxSendLevels organFxSends{}; // organ local mod/delay/reverb routing
    ModuleFxConfig synthFx{};
    ModuleFxConfig organFx{};
    bool masterDriveEnabled = false;
    float masterDriveAmount = 0.25f;
    bool masterAutoPanEnabled = false;
    float masterAutoPanRate = 1.5f;
    float masterAutoPanDepth = 0.45f;
    float cutoffHz = 20000.0f;
    float resonanceQ = 0.707f;
    float attackSec = 0.001f;
    float decaySec = 3.500f;
    float sustainLevel = 1.00f;
    float releaseSec = 0.350f;
    float loopHold = 0.65f;
    float volume = 0.85f;
    float pan = 0.0f;

    // Módulo 2: Sintetizador de 4 Capas
    bool synthEnabled = false;
    int synthPresetId = 1;
    float synthMasterVol = 0.80f;
    float synthPan = 0.f, synthTune = 0.f;
    float synthMacroCutoff = 1.0f;
    float synthWheelGlobalCutoff = 0.f;
    float synthWheelGlobalAmpDb = 0.f;
    float synthOutGainDb = 0.f;     // v32.4 module output trim (-24..+24 dB), saved with the preset
    int synthVoiceMode = 0;      // 0 poly, 1 mono/retrigger, 2 mono legato
    bool synthSeqGlobalClock = false;   // v32.3: the four layer sequencers share tempo + start
    bool synthSeqSharePattern = false;  // v32.3: layers B-D play layer A's pattern
    std::vector<ModConnectionSpec> modMatrix;  // v32.3 modulation matrix
    std::array<float,4> synthMacros{};         // v32.3 macro knobs 1-4
    bool synthPortamentoEnabled = false;
    float synthPortamento = 0.0f; // seconds
    int synthGlideMode = 1;      // 0 always, 1 only when keys overlap
    float synthSlide = 0.0f;     // transient pitch scoop, semitones
    SynthLayerConfig synthLayers[SYNTH_LAYER_COUNT];
    SynthLfoPatch synthLfos[4];
    SynthEnvelopePatch synthEnvelopes[3];
    // Persist main analog LFO with the eight GLOBAL programs (not just synth presets).
    bool synthLfoEnabled = false;
    int synthLfoWave = 0;
    float synthLfoRate = 4.5f;
    float synthLfoDepth = 0.0f;
    int synthLfoPitch = 0;       // cents at full depth
    int synthLfoCutoff = 0;      // Hz at full depth

    // Módulo 3: Órgano (B3 / Vox / Farfisa / Pipe)
    bool organEnabled = false;
    int organModel = 0;         // 0=B3, 1=Vox, 2=Farfisa, 3=Pipe
    float organVolume = 0.80f;
    float organPan = 0.f, organTune = 0.f;
    bool organRotaryFast = false;
    float organDrive = 0.12f;
    float organRotaryMix = 1.0f;
    float organRotaryFastRate = 1.0f;
    float organRotaryRamp = 1.0f;
    float organRotaryStereoWidth = 1.0f;
    float organVibratoDepth = 1.0f;
    float organVibratoRate = 6.8f;
    bool organChorusEnabled = false;
    float organChorusRate = 0.65f;
    float organChorusDepth = 0.55f;
    float organChorusMix = 0.30f;
    float organChorusWidth = 0.85f;
    float organChorusFeedback = 0.08f;
    float organChorusTone = 9000.0f;
    int organChorusVoices = 3;
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
    int chorusVoices = 2;
    float chorusWidth = 0.85f;
    float chorusFeedback = 0.08f;
    float chorusTone = 9500.0f;
    float chorusDelay = 16.0f;

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

    // New stereo flanger, individually bypassable in the master rack.
    bool flangerEnabled = false;
    float flangerRate = 0.35f, flangerDepth = 0.55f;
    float flangerFeedback = 0.25f, flangerMix = 0.35f;

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
    int sf2PresetIndex = -1; // SF2 phdr index; -1 uses first melodic program
    bool isV6 = true;
    int channels = 2;
    int zoneCount = 0;
    double memoryMB = 0.0;
    int releaseMode = 0;
    // Codec 4 sample metadata (only parsed; not mistaken for a program FX preset).
    struct SampleUnisonMetadata {
        bool available = false;
        uint8_t mode = 0, topKey = 0;
        std::array<uint16_t,4> detuneRaw{};
        std::array<uint8_t,3> pan{};
        std::array<uint8_t,4> voiceCounts{};
        std::array<uint32_t,4> gainRaw{};
        uint8_t randomStrokeMode = 0, blockRandomSustPed = 0;
    } sampleUnison;
    struct SampleVibratoMetadata {
        bool available = false, enabled = false;
        std::array<uint8_t,9> raw{}; // no undocumented conversion guessed
    } sampleVibrato;
    // Key-specific neighbouring recordings allowed on repeated notes.
    std::array<std::array<uint8_t,2>,128> repeatAlternateKeys{};
    std::array<uint8_t,128> repeatAlternateCounts{};
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

} // namespace SonusDSP