#pragma once
#include "SynthModMatrix.h"
#include "../dsp/Mt32Engine.h"
#include "../dsp/RtPublish.h"
#include "../dsp/StageDspEffects.h"
#include "../dsp/LayerInsertChain.h"
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <memory>

namespace SonusDSP {

constexpr int SYNTH_MAX_VOICES_PER_LAYER = 32;
constexpr int SYNTH_NUM_LFOS = 4;
constexpr int SYNTH_NUM_ENVS = 3;
constexpr int SYNTH_WAVETABLE_SIZE = 256;
constexpr int SYNTH_MAX_UNISON = 8;

struct Wavetable {
    std::string name = "Sine";
    std::array<float, SYNTH_WAVETABLE_SIZE> data{};
    bool custom = false;
    // Harmonic-bandlimited mip levels are generated before the audio callback.
    std::array<std::array<float,SYNTH_WAVETABLE_SIZE>,8> mip{};
};

struct SynthLFO {
    bool enabled = false;
    int   waveType = 0;          // 0=sine,1=tri,2=saw,3=sqr,4=S&H,5=smooth random
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
    bool  keySync = false;       // restart the cycle when a phrase starts (no keys held)
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
    float noiseFilterState = 0.0f;
    uint32_t noiseSeed = 1u;
    float filterIc1 = 0.0f;
    float filterIc2 = 0.0f;
    float highpassState = 0.0f;
    float cutoffSmooth = 8500.0f;
    float filterA1 = 1.0f;
    float filterA2 = 0.0f;
    float filterA3 = 0.0f;
    double freq = 440.0;
    double targetFreq = 440.0;
    float slidePitch = 0.0f;
    double phase1 = 0.0;
    double phase2 = 0.0;
    double phase3 = 0.0;
    double phaseSub = 0.0;
    float panOffset = 0.0f;
    float panGainL = 0.7071f;
    float panGainR = 0.7071f;
    // Last rendered oscillator contribution, for a bounded click-free
    // continuation if all voice slots are occupied during a new NoteOn.
    float lastOutputL = 0.0f, lastOutputR = 0.0f;
    float stealCarryL = 0.0f, stealCarryR = 0.0f;
    int stealFrames = 0, stealTotal = 0;
    float detuneMult = 1.0f;
    float phaseInit = 0.0f;
    int   sampleFrame = 0;
    float sampleLoopStart = 0;
    float sampleLoopEnd = 0;
    std::vector<float>* sampleDataL = nullptr;
    std::vector<float>* sampleDataR = nullptr;
    double sampleStep = 1.0;

    // v31: independent filter-envelope stage (1=attack, 2=decay/sustain, 4=release).
    int fStage = 0;
    // v31: 4-pole ladder / OTA filter (TPT one-pole stages) and its coefficient.
    std::array<float,4> ladder{};
    float ladderG = 0.5f;
    // v31: band-limited hard sync (post-step polyBLEP residual for the next sample).
    float syncCarry = 0.0f;
    // v31: extra oscillator state (hypersaw side saws, pink noise filters).
    std::array<double,6> hyperPhase{};
    std::array<float,3> pink{};
    // v31: analog drift.
    float driftCents = 0.0f, driftTarget = 0.0f, staticCents = 0.0f, cutoffSpread = 1.0f;
    float driftRatio = 1.0f;
    int driftTimer = 0;
    // v31: FM4 operators.
    std::array<double,4> opPhase{};
    std::array<float,4> opEnv{};
    std::array<int,4> opStage{};
    std::array<float,4> opScale{{1.f,1.f,1.f,1.f}};
    float fbPrev1 = 0.0f, fbPrev2 = 0.0f;
    // v31: attack transient.
    float transEnv = 0.0f;
    double transPhase = 0.0;
    float transBp1 = 0.0f, transBp2 = 0.0f, transLp = 0.0f;
    // v32.3 per-voice modulation (computed every 16 samples).
    float modCutMul = 1.f, modAmpMul = 1.f, modPanAdd = 0.f, modPitchMul = 1.f;
    // v32: panic / all-notes-off fades the voice out in 12 ms (never a hard cut).
    bool fastRelease = false;
    float killFade = 1.0f;
};

class SingleSynthLayer {
public:
    SynthLayerConfig cfg;
    float performancePitchBendSemis = 0.f;
    float performanceModWheel = 0.f;
    float performanceWheelGlobalCutoff = 0.f;
    float performanceWheelGlobalAmpDb = 0.f;
    int voiceMode = 0;
    int voiceLimit = SYNTH_MAX_VOICES_PER_LAYER;
    std::vector<std::array<int,3>> heldKeys; // [raw,played,velocity] per layer
    bool glideEnabled = false;
    float glideSeconds = 0.0f;
    int glideMode = 1;
    float slideSemis = 0.0f;
    std::array<SynthPolyVoice, SYNTH_MAX_VOICES_PER_LAYER> voices{};
    // v32: set by the module while a layer switched OFF fades out (renders anyway).
    bool renderWhileDisabled = false;
    // v32.3: glide time requested by the sequencer for a SLIDE step (0 = none).
    float seqGlideSeconds = 0.f;
    // v32.3 modulation: the configuration rendered this block (cfg + global modulation),
    // the matrix snapshot and the global source values (set by the module per block).
    SynthLayerConfig runCfg;
    int index = 0;
    const ModMatrixSnapshot* modSnap = nullptr;
    std::array<float, MOD_SOURCE_COUNT> modGlobal{};
    // v32.2: DX7 engine for synthesisType 5 (allocated in init(), never on the audio thread).
    std::shared_ptr<Dx7LayerEngine> dx7;
    uint32_t dx7AppliedRevision = ~0u;
    std::vector<float> dx7Buffer;
    // v32.2: MT-32 engine for synthesisType 6 (null without the user's ROMs).
    std::shared_ptr<Mt32LayerEngine> mt32;
    std::vector<float> mt32BufferR;

    void init(float sr);
    void reset();
    /** Fades every sounding voice out in 12 ms (all-notes-off without clicks). */
    void releaseAll();
    void noteOn(int rawKey, int playedKey, int velocity);
    void noteOff(int rawKey, bool pedalDown);
    void setSustain(bool down);
    void renderLayer(float* mixL, float* mixR, unsigned int numFrames, float sr,
                     float macroCutoff, const std::vector<Wavetable>& wtBank,
                     SynthLFO* lfos, const SynthEnvelope* envs);

private:
    // Puts every envelope of a voice into its release stage.
    static void releaseVoice(SynthPolyVoice& v);
    // v31 LFO state: delay/fade timer, S&H and smooth-random values.
    bool lfoRetrigger_ = false;
    double lfoElapsed_ = 0.0;
    float lfoHeld_ = 0.0f, lfoPrevRandom_ = 0.0f, lfoNextRandom_ = 0.0f;
    uint32_t lfoSeed_ = 0x2545F491u;
    float sr_ = 48000.0f;
    StateVariableFilter filter;
    float smoothedCutoff_ = 8500.0f;
    float smoothedQ_ = 0.85f;
    double pwmPhase_ = 0.0;
    double wheelVibratoPhase_ = 0.0; // Separate oscillator: CC1 vibrato must not follow PWM speed
    double lastNoteFrequency_ = 0.0;
    const std::vector<Wavetable>* wtBank_ = nullptr;

    inline float polyBLEP(double t, double dt);
    inline float naiveSaw(double phase);
    inline float naiveSquare(double phase);
    float readWavetable(int idx, double phase, double step) const;
};

class StageSynthModule {
public:
    bool  synthEnabled = false;
    int   synthPresetId = 1;
    float synthMasterVol = 0.8f;
    float masterBpm = 120.f; // Shared BPM from host, updated per audio block
    // Set on the audio callback thread before processing MIDI events.
    // Four layers share a finite voice allocation with piano and organ.
    int voiceLimitPerLayer = SYNTH_MAX_VOICES_PER_LAYER;
    float synthMacroCutoff = 1.0f;
    float synthWheelGlobalCutoff = 0.f;
    float synthWheelGlobalAmpDb = 0.f;
    float synthOutGainDb = 0.f;
    bool synthVoiceMaster = true;
    int synthKeyLow = 0, synthKeyHigh = 127;
    int synthVoiceMode = 0;
    bool synthSeqGlobalClock = false;
    bool synthSeqSharePattern = false;
    // v32.3 modulation sources owned by the module / set by the host per block.
    std::array<float,4> macros{};
    float modBend = 0.f, modAftertouch = 0.f, modRandom = 0.f;
    /** Control threads: replace the matrix (invalid connections are dropped). */
    void setModMatrix(const std::vector<ModConnectionSpec>& specs);
    std::vector<ModConnectionSpec> getModMatrix() const;
    bool synthPortamentoEnabled = false;
    float synthPortamento = 0.0f;
    int synthGlideMode = 1;
    float synthSlide = 0.0f;
    struct HeldNote { int rawKey, playedKey, velocity; };
    std::vector<HeldNote> monoHeldNotes; // retained for legacy callers

    std::array<SingleSynthLayer, SYNTH_LAYER_COUNT> layers;

    std::array<SynthLFO, SYNTH_NUM_LFOS> lfos{};
    std::array<SynthEnvelope, SYNTH_NUM_ENVS> envelopes{};
    std::vector<Wavetable> wavetableBank;
    std::array<SynthLayerInsertDSP, SYNTH_LAYER_COUNT> layerInsertDSP;
    std::mutex wtMutex;
    // Immutable wavetable snapshot published lock-free (RtPublish.h): the audio
    // thread reads a raw pointer, replaced banks are freed on the control thread.
    AudioEpoch audioEpoch_;
    RtPublished<std::vector<Wavetable>> wavetables_{audioEpoch_};
    RtPublished<ModMatrixSnapshot> modMatrix_{audioEpoch_};
    std::vector<ModConnectionSpec> modSpecs_;
    mutable std::mutex modMutex_;
    uint32_t randomSeed_ = 0x9E3779B9u;

    void init(float sr);
    void loadBuiltInSynthPreset(int presetId);
    /** layerMask (v33 MIDI routing): bit n = layer n (A..D); default every layer. */
    void noteOn(int rawKey, int playedKey, int velocity, unsigned layerMask = 0xFu);
    void noteOff(int rawKey, bool pedalDown, unsigned layerMask = 0xFu);
    void setSustain(bool down);
    void allNotesOff();
    void render(float* outInterleavedStereo, unsigned int numFrames,
                const std::array<float*, SYNTH_LAYER_COUNT>* layerStems = nullptr);

    void addWavetable(const Wavetable& wt);
    void clearWavetables();
    void generateDefaultWavetables();
    const std::vector<Wavetable>& getWavetableBank() const { return wavetableBank; } // offline/test access
    std::shared_ptr<const std::vector<Wavetable>> getWavetableSnapshot() const { return wavetables_.owner(); }
    /** Audio to produce: enabled with level, or still fading out after OFF.
        The host skips the synth (no CPU) when this is false. */
    // v32.4 patch-change hold: whole-patch writes from control threads (preset, program,
    // layer INIT / PASTE, DX7 voice) first fade the synth out (~10 ms); while held, the
    // audio thread keeps its last consistent per-block config instead of copying a
    // half-written one. Nested holds are allowed. See PatchChangeGuard.
    std::atomic<int> patchHold_{0};
    /** MIDI START: restart every sequencer on the next block (audio thread consumes it). */
    void requestSeqRestart() { seqRestart_.store(true, std::memory_order_release); }
    std::atomic<bool> seqRestart_{false};
    std::atomic<float> masterLevel_{0.f};       // published per block for the guard
    std::atomic<uint64_t> renderBlocks_{0};     // audio is running when this advances
    std::atomic<int64_t> lastRenderNs_{0};      // steady-clock time of the last block
    std::atomic<std::thread::id> renderThread_{};   // the thread that renders (audio callback)
    bool needsRender() const { return (synthEnabled && synthMasterVol > 0.001f) || masterSmooth_ > 1e-6f; }
    /** Voices currently allocated in all layers. */
    int activeVoices() const { int n = 0; for (const auto& l : layers) for (const auto& v : l.voices) n += v.active ? 1 : 0; return n; }

    void setLFO(int idx, const SynthLFO& lfo);
    void setEnvelope(int idx, const SynthEnvelope& env);
    void updateLFOsAndEnvs(unsigned int numFrames, float sr);

private:
    float sr_ = 48000.0f;
    std::vector<float> bufL_, bufR_;
    std::array<double,SYNTH_LAYER_COUNT> seqPhase_{};
    std::array<float,SYNTH_LAYER_COUNT> seqAmp_{{1.f,1.f,1.f,1.f}};
public:
    /** Step each layer's sequencer is playing (-1 = stopped), for the UI. */
    std::array<std::atomic<int>,SYNTH_LAYER_COUNT> seqStep_{{-1,-1,-1,-1}};
private:
    // Stop rendering idle insert racks after their audible tails have decayed.
    std::array<bool,SYNTH_LAYER_COUNT> insertTailActive_{};
    std::array<unsigned int,SYNTH_LAYER_COUNT> insertSilentFrames_{};
    // v32 live smoothing: master level / module ON-OFF and per-layer ON-OFF ramps.
    std::vector<float> masterRamp_;
    float masterSmooth_ = 0.0f;
    std::array<float,SYNTH_LAYER_COUNT> layerGate_{};
    std::array<float,SYNTH_LAYER_COUNT> outGain_{{1.f,1.f,1.f,1.f}};   // smoothed per-layer output trim
    // v32.4 layer EQ (MasterEqualizer, the piano Sound EQ): coefficients are recomputed only
    // when the layer's EQ settings change; ON / OFF crossfades the EQ'd signal in 10 ms.
    struct EqState {
        MasterEqualizer eq{};
        float gate = 0.f;
        bool valid = false;
        float hp = 0.f, lp = 0.f, f[3]{}, q[3]{}, g[3]{};
        int hpS = 0, lpS = 0;
    };
    std::array<EqState,SYNTH_LAYER_COUNT> layerEq_{};
};

/** RAII: fade the synth out before rewriting a whole patch, back in afterwards.
 *  Waits (max ~30 ms) only when the audio callback is running and the synth is audible. */
class PatchChangeGuard {
public:
    explicit PatchChangeGuard(StageSynthModule& m) : m_(m) {
        m_.patchHold_.fetch_add(1, std::memory_order_acq_rel);
        if (m_.masterLevel_.load(std::memory_order_acquire) <= 0.f) return;
        // Called from the rendering thread itself (offline render, tests): nothing to wait for.
        if (m_.renderThread_.load(std::memory_order_relaxed) == std::this_thread::get_id()) return;
        // No block rendered in the last 50 ms: no live audio callback (offline / tests).
        const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - m_.lastRenderNs_.load(std::memory_order_relaxed) > 50'000'000) return;
        // Deadline-based wait (sleep_for(1 ms) can last ~15 ms on Windows): yield until the
        // audio thread reports silence and has rendered one more block, max 30 ms.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(30);
        uint64_t silentAt = 0; bool silent = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!silent && m_.masterLevel_.load(std::memory_order_acquire) <= 0.f) {
                silent = true; silentAt = m_.renderBlocks_.load(std::memory_order_relaxed);
            }
            if (silent && m_.renderBlocks_.load(std::memory_order_relaxed) != silentAt) return;
            std::this_thread::yield();
        }
    }
    ~PatchChangeGuard() { m_.patchHold_.fetch_sub(1, std::memory_order_acq_rel); }
    PatchChangeGuard(const PatchChangeGuard&) = delete;
    PatchChangeGuard& operator=(const PatchChangeGuard&) = delete;
private:
    StageSynthModule& m_;
};

} // namespace SonusDSP