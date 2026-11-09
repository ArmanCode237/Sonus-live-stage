#pragma once
#include "NordTypes.h"
#include "NordDspEffects.h"
#include <array>
#include <atomic>
#include <mutex>
#include <memory>

namespace NordDSP {

enum class KbdTouch { Light = 0, Normal = 1, Heavy = 2 };
enum class EnvStage { Idle = 0, Attack, Decay, Sustain, Release, FastStealFade };

struct Voice {
    bool active = false;
    int midiKey = -1;
    int playedMidiKey = -1;
    int velocity = 100;
    bool keyHeld = false;
    bool sustainedByPedal = false;
    EnvStage stage = EnvStage::Idle;
    uint64_t ageOrder = 0;

    const StrokeSample* sample = nullptr;
    double pos = 0.0;
    double step = 1.0;

    float velocityGain = 1.0f;
    float sampleEnv = 1.0f;
    float adsrLevel = 0.0f;
    float baseLoopDecayPerSample = 1.0f;
    bool inLoop = false;
};

struct QueuedMidiEvent {
    uint8_t type = 0;
    int data1 = 0;
    int data2 = 0;
};

class NordPianoEngine {
public:
    static constexpr size_t MAX_VOICES = 256;
    static constexpr size_t MIDI_QUEUE_SIZE = 512;

    NordPianoEngine();

    void setSampleRate(double sr);
    void setInstrument(std::shared_ptr<const PianoInstrument> inst);
    void setInstrument(const PianoInstrument* inst);
    std::shared_ptr<const PianoInstrument> getInstrument() const;

    void applyPreset(const SynthPresetParams& p);

    void noteOn(int midiKey, int velocity);
    void noteOff(int midiKey);
    void setSustainPedal(bool down);
    void allNotesOff();
    void triggerTestTone();

    void setMaxVoices(int maxV) { maxVoicesLimit_.store(std::clamp(maxV, 16, static_cast<int>(MAX_VOICES))); }
    int getMaxVoices() const { return maxVoicesLimit_.load(); }
    void setTranspose(int semis) { transposeSemis_.store(std::clamp(semis, -12, 12)); }
    void setOctaveShift(int oct) { octaveShift_.store(std::clamp(oct, -2, 2)); }
    void setMasterTuneCents(float cents) { masterTuneCents_.store(std::clamp(cents, -100.0f, 100.0f)); }

    void setMasterGain(float linearGain) { masterGain_.store(std::clamp(linearGain, 0.0f, 2.0f)); }
    void setPan(float pan) { pan_.store(std::clamp(pan, -1.0f, 1.0f)); }
    void setCutoffHz(float hz) { cutoffHz_.store(std::clamp(hz, 20.0f, 20000.0f)); filterDirty_.store(true); }
    void setResonanceQ(float q) { resonanceQ_.store(std::clamp(q, 0.5f, 15.0f)); filterDirty_.store(true); }

    void setAttackSec(float sec) { attackSec_.store(std::clamp(sec, 0.001f, 10.0f)); }
    void setDecaySec(float sec) { decaySec_.store(std::clamp(sec, 0.005f, 15.0f)); }
    void setSustainLevel(float lvl) { sustainLevel_.store(std::clamp(lvl, 0.0f, 1.0f)); }
    void setReleaseSec(float sec) { releaseSec_.store(std::clamp(sec, 0.005f, 15.0f)); }
    void setLoopHold(float hold) { loopHold_.store(std::clamp(hold, 0.0f, 1.0f)); }

    void setEqEnabled(bool en) { eqEnabled_.store(en); eqDirty_.store(true); }
    void setLowCutHz(float hz) { lowCutHz_.store(std::clamp(hz, 20.0f, 1000.0f)); eqDirty_.store(true); }
    void setHighCutHz(float hz) { highCutHz_.store(std::clamp(hz, 1000.0f, 20000.0f)); eqDirty_.store(true); }
    void setEqBandGainDb(int bandIdx, float db);
    void setEqBandFreqHz(int bandIdx, float hz);
    void setEqBandQ(int bandIdx, float q);

    void setChorusEnabled(bool en) { chorusEnabled_.store(en); }
    void setChorusRate(float hz) { chorusRate_.store(std::clamp(hz, 0.1f, 8.0f)); }
    void setChorusDepth(float d) { chorusDepth_.store(std::clamp(d, 0.0f, 1.0f)); }
    void setChorusMix(float m) { chorusMix_.store(std::clamp(m, 0.0f, 1.0f)); }

    void setPhaserEnabled(bool en) { phaserEnabled_.store(en); }
    void setPhaserRate(float hz) { phaserRate_.store(std::clamp(hz, 0.1f, 8.0f)); }
    void setPhaserDepth(float d) { phaserDepth_.store(std::clamp(d, 0.0f, 1.0f)); }
    void setPhaserFeedback(float fb) { phaserFeedback_.store(std::clamp(fb, 0.0f, 0.85f)); }
    void setPhaserMix(float m) { phaserMix_.store(std::clamp(m, 0.0f, 1.0f)); }

    void setTremoloEnabled(bool en) { tremoloEnabled_.store(en); }
    void setTremoloRate(float hz) { tremoloRate_.store(std::clamp(hz, 0.5f, 16.0f)); }
    void setTremoloDepth(float d) { tremoloDepth_.store(std::clamp(d, 0.0f, 1.0f)); }

    void setDelayEnabled(bool en) { delayEnabled_.store(en); }
    void setDelayTimeMs(float ms) { delayTimeMs_.store(std::clamp(ms, 20.0f, 1500.0f)); }
    void setDelayFeedback(float fb) { delayFeedback_.store(std::clamp(fb, 0.0f, 0.92f)); }
    void setDelayDamping(float d) { delayDamping_.store(std::clamp(d, 0.0f, 1.0f)); }
    void setDelayMix(float m) { delayMix_.store(std::clamp(m, 0.0f, 1.0f)); }
    void setDelayPingPong(bool pp) { delayPingPong_.store(pp); }

    // Reverb Clásica y ShimmerReverbEngine
    void setReverbEnabled(bool en) { reverbEnabled_.store(en); }
    void setReverbType(int t) { reverbType_.store(std::clamp(t, 0, 3)); shimmerDirty_.store(true); }
    void setReverbMix(float m) { reverbMix_.store(std::clamp(m, 0.0f, 1.0f)); shimmerDirty_.store(true); }
    void setReverbSize(float sz) { reverbSize_.store(std::clamp(sz, 0.05f, 1.0f)); shimmerDirty_.store(true); }
    void setReverbDamping(float d) { reverbDamping_.store(std::clamp(d, 0.0f, 1.0f)); }
    void setReverbPreDelayMs(float ms) { reverbPreDelayMs_.store(std::clamp(ms, 0.0f, 240.0f)); shimmerDirty_.store(true); }
    void setReverbShimmer(float s) { reverbShimmer_.store(std::clamp(s, 0.0f, 1.0f)); }

    // Controles específicos de ShimmerReverbEngine
    void setShimmerShiftSt(float st) { shimmerShiftSt_.store(std::clamp(st, -12.0f, 12.0f)); shimmerDirty_.store(true); }
    void setShimmerFeedback(float fb) { shimmerFeedback_.store(std::clamp(fb, 0.0f, 0.985f)); shimmerDirty_.store(true); }
    void setShimmerDiffusion(float df) { shimmerDiffusion_.store(std::clamp(df, 0.0f, 0.98f)); shimmerDirty_.store(true); }
    void setShimmerLowCutHz(float hz) { shimmerLowCutHz_.store(std::clamp(hz, 20.0f, 1000.0f)); shimmerDirty_.store(true); }
    void setShimmerHighCutHz(float hz) { shimmerHighCutHz_.store(std::clamp(hz, 1000.0f, 20000.0f)); shimmerDirty_.store(true); }
    void setShimmerModRateHz(float hz) { shimmerModRateHz_.store(std::clamp(hz, 0.05f, 5.0f)); shimmerDirty_.store(true); }
    void setShimmerModDepth(float dp) { shimmerModDepth_.store(std::clamp(dp, 0.0f, 1.0f)); shimmerDirty_.store(true); }
    void setShimmerPitchMode(int pm) { shimmerPitchMode_.store(std::clamp(pm, 0, 4)); shimmerDirty_.store(true); }
    void setShimmerReverbMode(int rm) { shimmerReverbMode_.store(std::clamp(rm, 0, 3)); shimmerDirty_.store(true); }
    void setShimmerColorMode(int cm) { shimmerColorMode_.store(std::clamp(cm, 0, 1)); shimmerDirty_.store(true); }

    void setSoftRelease(bool enabled) { softRelease_.store(enabled); }
    void setKbdTouch(KbdTouch touch) { kbdTouch_.store(static_cast<int>(touch)); }

    SynthPresetParams getCurrentParams() const;
    int getActiveVoiceCount() const { return activeVoicesCount_.load(); }
    float getPeakLeft() const { return peakL_.load(); }
    float getPeakRight() const { return peakR_.load(); }
    bool isClipping() const { return clipping_.load(); }
    bool isPedalDown() const { return sustainPedalState_.load(); }
    int getLastMidiNote() const { return lastMidiNote_.load(); }
    int getLastMidiVel() const { return lastMidiVel_.load(); }
    int getLastStrokeIndex() const { return lastTriggeredStrokeIdx_.load(); }
    uint64_t getMidiEventSeq() const { return midiEventSeq_.load(); }

    void getActiveKeyStates(std::array<uint8_t, 128>& outStates) const;
    void render(float* outInterleavedStereo, unsigned int numFrames);

private:
    void pushMidiEvent(uint8_t type, int d1, int d2);
    void handleNoteOnInternal(const PianoInstrument& inst, int rawMidiKey, int velocity);
    void handleNoteOffInternal(int rawMidiKey);
    void handleSustainInternal(bool down);

    const StrokeSample* selectStrokeFor(const PianoInstrument& inst, int midiKey, int velocity) const;
    int touchAttenuation(int velocity) const;

    double hostSampleRate_ = 48000.0;
    std::shared_ptr<const PianoInstrument> instrument_;
    mutable std::mutex instPtrMutex_;

    std::array<Voice, MAX_VOICES> voices_;
    bool sustainPedalDown_ = false;
    uint64_t voiceCounter_ = 0;

    std::array<QueuedMidiEvent, MIDI_QUEUE_SIZE> midiQueue_;
    std::atomic<size_t> midiWriteIdx_{0};
    std::atomic<size_t> midiReadIdx_{0};

    std::atomic<uint32_t> testToneFramesRemaining_{0};
    double testTonePhase_ = 0.0;

    std::atomic<int> maxVoicesLimit_{128};
    std::atomic<int> transposeSemis_{0};
    std::atomic<int> octaveShift_{0};
    std::atomic<float> masterTuneCents_{0.0f};

    std::atomic<float> masterGain_{0.85f};
    std::atomic<float> pan_{0.0f};
    std::atomic<float> cutoffHz_{20000.0f};
    std::atomic<float> resonanceQ_{0.707f};
    std::atomic<float> attackSec_{0.001f};
    std::atomic<float> decaySec_{3.500f};
    std::atomic<float> sustainLevel_{1.00f};
    std::atomic<float> releaseSec_{0.350f};
    std::atomic<float> loopHold_{0.65f};

    std::atomic<bool> eqEnabled_{true};
    std::atomic<float> lowCutHz_{20.0f};
    std::atomic<float> highCutHz_{20000.0f};
    std::array<std::atomic<float>, 5> eqFreqHz_;
    std::array<std::atomic<float>, 5> eqQ_;
    std::array<std::atomic<float>, 5> eqGainDb_;

    std::atomic<bool> chorusEnabled_{false};
    std::atomic<float> chorusRate_{0.8f};
    std::atomic<float> chorusDepth_{0.45f};
    std::atomic<float> chorusMix_{0.35f};

    std::atomic<bool> phaserEnabled_{false};
    std::atomic<float> phaserRate_{0.6f};
    std::atomic<float> phaserDepth_{0.60f};
    std::atomic<float> phaserFeedback_{0.40f};
    std::atomic<float> phaserMix_{0.35f};

    std::atomic<bool> tremoloEnabled_{false};
    std::atomic<float> tremoloRate_{4.5f};
    std::atomic<float> tremoloDepth_{0.50f};

    std::atomic<bool> delayEnabled_{false};
    std::atomic<float> delayTimeMs_{360.0f};
    std::atomic<float> delayFeedback_{0.35f};
    std::atomic<float> delayDamping_{0.35f};
    std::atomic<float> delayMix_{0.25f};
    std::atomic<bool> delayPingPong_{true};

    std::atomic<bool> reverbEnabled_{true};
    std::atomic<int> reverbType_{2};
    std::atomic<float> reverbMix_{0.28f};
    std::atomic<float> reverbSize_{0.65f};
    std::atomic<float> reverbDamping_{0.40f};
    std::atomic<float> reverbPreDelayMs_{18.0f};
    std::atomic<float> reverbShimmer_{0.45f};

    // Estado atómico de ShimmerReverbEngine
    std::atomic<float> shimmerShiftSt_{12.0f};
    std::atomic<float> shimmerFeedback_{0.60f};
    std::atomic<float> shimmerDiffusion_{0.88f};
    std::atomic<float> shimmerLowCutHz_{140.0f};
    std::atomic<float> shimmerHighCutHz_{8500.0f};
    std::atomic<float> shimmerModRateHz_{0.45f};
    std::atomic<float> shimmerModDepth_{0.22f};
    std::atomic<int> shimmerPitchMode_{1};
    std::atomic<int> shimmerReverbMode_{2};
    std::atomic<int> shimmerColorMode_{1};

    std::atomic<bool> filterDirty_{true};
    std::atomic<bool> eqDirty_{true};
    std::atomic<bool> shimmerDirty_{true};
    std::atomic<bool> softRelease_{false};
    std::atomic<int> kbdTouch_{static_cast<int>(KbdTouch::Normal)};

    std::atomic<int> activeVoicesCount_{0};
    std::atomic<float> peakL_{0.0f};
    std::atomic<float> peakR_{0.0f};
    std::atomic<bool> clipping_{false};
    std::atomic<bool> sustainPedalState_{false};
    std::atomic<int> lastMidiNote_{-1};
    std::atomic<int> lastMidiVel_{0};
    std::atomic<int> lastTriggeredStrokeIdx_{0};
    std::atomic<uint64_t> midiEventSeq_{0};

    std::array<std::atomic<uint8_t>, 128> keyStates_;

    StateVariableFilter svfFilter_;
    MasterEqualizer masterEq_;
    ModulationRack modRack_;
    StereoDelayEffect delayFx_;
    NordReverbEffect reverbFx_;
    ShimmerReverbEngine shimmerReverbFx_;
};

} // namespace NordDSP