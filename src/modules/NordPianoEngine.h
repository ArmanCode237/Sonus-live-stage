#pragma once
#include "../core/NordTypes.h"
#include "../dsp/NordDspEffects.h"
#include "../dsp/NordSlotFxRack.h"
#include "../dsp/RtPublish.h"
#include <array>
#include <atomic>
#include <mutex>
#include <memory>
#include "NordSynthModule.h"
#include "NordOrganModule.h"

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
    std::shared_ptr<const PianoInstrument> sampleBank; // keeps samples alive after bank changes
    int pianoLayer = 0;
    double pos = 0.0;
    double step = 1.0;
    double pitchCurrent = 1.0;
    double pitchTarget = 1.0;
    double wheelVibratoPhase = 0.0;
    float wheelVibratoAmount = 0.0f;

    float velocityGain = 1.0f;
    float targetVelocityGain = 1.0f;
    float streamFade = 1.0f;
    float fastStealFadeStep = 0.f;
    float sampleEnv = 1.0f;
    float adsrLevel = 0.0f;
    float baseLoopDecayPerSample = 1.0f;
    bool inLoop = false; // the playback head has wrapped at least once
    bool loopDecayActive = false; // NSMP enters decay at first loopStart
    bool isReleaseVoice = false;
    float lastRenderedL = 0.f;
    float lastRenderedR = 0.f;
    // v32: bank format cached at voice start (render() compared strings per voice per block).
    bool bankSf2 = false;
    bool bankNsmp = false;
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
    void setLayerInstrument(int layer, std::shared_ptr<const PianoInstrument> inst);
    std::shared_ptr<const PianoInstrument> getLayerInstrument(int layer) const;
    std::array<std::shared_ptr<const PianoInstrument>, PIANO_LAYER_COUNT> getPianoInstruments() const;
    void setPianoVolume(float level) { pianoVolume_.store(std::clamp(level, 0.0f, 1.5f)); }
    void setPianoLayerEnabled(int layer, bool enabled) { if (layer >= 0 && layer < PIANO_LAYER_COUNT) pianoLayerEnabled_[layer].store(enabled); }
    void setPianoLayerVolume(int layer, float volume) { if (layer >= 0 && layer < PIANO_LAYER_COUNT) pianoLayerVolume_[layer].store(std::clamp(volume, 0.0f, 1.5f)); }

    void setModuleFxSend(int layer, int bus,float level) {
        if(layer>=0&&layer<SYNTH_LAYER_COUNT&&bus>=0&&bus<FX_BUS_COUNT)
            synthFxSend_[layer][bus].store(std::clamp(level,0.f,1.f));
    }
    void setOrganFxSend(int bus,float level){
        if(bus>=0&&bus<3) organFxSend_[bus].store(std::clamp(level,0.f,1.f));
    }
    void setModuleFxParam(bool synth,int field,float value);
    ModuleFxConfig getModuleFxSnapshot(bool synth) const;
    void setMasterColorParam(int field,float v) {
        if(field==0) masterDriveOn_.store(v>=.5f);
        else if(field==1) masterDriveAmt_.store(std::clamp(v,0.f,1.f));
        else if(field==2) masterAutoPanOn_.store(v>=.5f);
        else if(field==3) masterAutoPanRate_.store(std::clamp(v,.05f,12.f));
        else if(field==4) masterAutoPanDepth_.store(std::clamp(v,0.f,1.f));
    }
    void setPianoZone(int layer,bool high,int key) {
        if(layer>=0 && layer<PIANO_LAYER_COUNT) (high?pianoZoneHigh_[layer]:pianoZoneLow_[layer]).store(std::clamp(key,0,127));
    }
    void setModuleZone(int module,bool high,int key) {
        if(module>=0&&module<3) (high?moduleZoneHigh_[module]:moduleZoneLow_[module]).store(std::clamp(key,0,127));
    }
    void setFxSend(int source, int bus, float amount) {
        if (source >= 0 && source < FX_SOURCE_COUNT && bus >= 0 && bus < FX_BUS_COUNT)
            fxSends_[source][bus].store(std::clamp(amount, 0.0f, 1.0f));
    }
    // V7.2 studio racks. All mutations occur on the control thread; the audio
    // thread reads atomic settings and prebuilt per-slot DSP instances only.
    void setStudioRoutingEnabled(bool /*enabled*/) { studioRoutingEnabled_.store(true); }
    bool studioRoutingEnabled() const { return studioRoutingEnabled_.load(); }
    bool setStudioSlot(int rack, int slot, int field, float value);
    bool setStudioSend(int rack, int source, int slot, float amount);
    bool setStudioEq(int rack, int band, float gain);
    bool setStudioEqEnabled(int rack, bool enabled);
    bool setStudioEqParam(int rack,int field,float value);
    StudioRackConfig getStudioRack(int rack) const;
    void setStudioRack(int rack, const StudioRackConfig& cfg);
    bool swapStudioSlots(int rack,int first,int second);
    void setSamplePortamento(int layer,int field,float value) {
        if(layer<0||layer>=PIANO_LAYER_COUNT)return;
        if(field==0)sampleGlideOn_[layer].store(value>=.5f);
        else if(field==1)sampleGlideTime_[layer].store(std::clamp(value,0.f,2.f));
        else if(field==2)sampleGlideMode_[layer].store(std::clamp(int(value),0,1));
    }
    void setPianoLayerSampleParam(int layer, int param, float value);
    void setSoundEqParameter(int layer,int field,float value); // 0 enable, 1 HP, 2 LP, 3/4 slopes, 5..13 bands

    void setPianoWheelRoute(int layer,int field,float amount) {
        if(layer<0||layer>=PIANO_LAYER_COUNT)return;
        if(field==0)pianoWheelCutoff_[layer].store(std::clamp(amount,-6.f,6.f));
        else if(field==1)pianoWheelAmpDb_[layer].store(std::clamp(amount,-24.f,12.f));
        else if(field==2)pianoWheelCutoffEnabled_[layer].store(amount>=.5f);
        else if(field==3)pianoWheelCutoffInvert_[layer].store(amount>=.5f);
        else if(field==4)pianoWheelAmpEnabled_[layer].store(amount>=.5f);
        else if(field==5)pianoWheelVibratoEnabled_[layer].store(amount>=.5f);
        else if(field==6)pianoWheelVibratoCents_[layer].store(std::clamp(amount,0.f,200.f));
        else if(field==7)pianoWheelVibratoRate_[layer].store(std::clamp(amount,.1f,12.f));
    }
    void setPianoVoiceMode(int layer, int field, float value) {
        if(layer<0||layer>=PIANO_LAYER_COUNT)return;
        if(field==0)pianoMonoMode_[layer].store(value>=.5f);
        else if(field==1)pianoLegatoEnabled_[layer].store(value>=.5f);
    }
    void setModWheel(float normalized) { modWheel_.store(std::clamp(normalized, 0.0f, 1.0f)); }
    /** v32.3: MIDI channel pressure (0..1), a modulation source of the synth. */
    void setAftertouch(float normalized) { aftertouch_.store(std::clamp(normalized, 0.0f, 1.0f)); }
    void setPitchBend(float normalized) { pitchBend_.store(std::clamp(normalized, -1.0f, 1.0f)); }
    float getModWheel() const { return modWheel_.load(); }
    float getPitchBend() const { return pitchBend_.load(); }
    void setMasterBpm(float bpm) { masterBpm_.store(std::clamp(bpm,30.f,300.f)); }
    void setDelayTempoSync(bool sync) { delayTempoSync_.store(sync); }
    void setDelayDivision(int index) { delayDivision_.store(std::clamp(index,0,15)); }
    void applyPreset(const SynthPresetParams& p);

    void noteOn(int midiKey, int velocity);
    void noteOff(int midiKey);
    void setSustainPedal(bool down);
    void allNotesOff();
    void triggerTestTone();

    void setPianoEnabled(bool en) { pianoEnabled_.store(en); }
    bool isPianoEnabled() const { return pianoEnabled_.load(); }

    void setMaxVoices(int maxV) { maxVoicesLimit_.store(std::clamp(maxV, 16, static_cast<int>(MAX_VOICES))); }
    int getMaxVoices() const { return maxVoicesLimit_.load(); }
    void setTranspose(int semis) { transposeSemis_.store(std::clamp(semis, -12, 12)); }
    void setOctaveShift(int oct) { octaveShift_.store(std::clamp(oct, -2, 2)); }
    void setMasterTuneCents(float cents) { masterTuneCents_.store(std::clamp(cents, -100.0f, 100.0f)); }

    void setModulePan(int module,float pan) { if(module>=0&&module<3)modulePan_[module].store(std::clamp(pan,-1.f,1.f)); }
    void setModuleTune(int module,float cents) { if(module>=0&&module<3)moduleTune_[module].store(std::clamp(cents,-100.f,100.f)); }
    float modulePan(int module)const{return module>=0&&module<3?modulePan_[module].load():0.f;}
    float moduleTune(int module)const{return module>=0&&module<3?moduleTune_[module].load():0.f;}
    void setMasterGain(float linearGain) { masterGain_.store(std::clamp(linearGain, 0.0f, 2.0f)); }
    void setPan(float pan) { pan_.store(std::clamp(pan, -1.0f, 1.0f)); }
    void setCutoffHz(float hz) { cutoffHz_.store(std::clamp(hz, 20.0f, 20000.0f)); filterDirty_.store(true); }
    void setResonanceQ(float q) { resonanceQ_.store(std::clamp(q, 0.5f, 15.0f)); filterDirty_.store(true); }

    void setAttackSec(float sec) { attackSec_.store(std::clamp(sec, 0.001f, 10.0f)); for(int i=0;i<PIANO_LAYER_COUNT;++i) setPianoLayerSampleParam(i,0,attackSec_.load()); }
    void setDecaySec(float sec) { decaySec_.store(std::clamp(sec, 0.005f, 15.0f)); for(int i=0;i<PIANO_LAYER_COUNT;++i) setPianoLayerSampleParam(i,1,decaySec_.load()); }
    void setSustainLevel(float lvl) { sustainLevel_.store(std::clamp(lvl, 0.0f, 1.0f)); for(int i=0;i<PIANO_LAYER_COUNT;++i) setPianoLayerSampleParam(i,2,sustainLevel_.load()); }
    void setReleaseSec(float sec) { releaseSec_.store(std::clamp(sec, 0.005f, 15.0f)); for(int i=0;i<PIANO_LAYER_COUNT;++i) setPianoLayerSampleParam(i,3,releaseSec_.load()); }
    void setLoopHold(float hold) { loopHold_.store(std::clamp(hold, 0.0f, 1.0f)); for(int i=0;i<PIANO_LAYER_COUNT;++i) setPianoLayerSampleParam(i,4,loopHold_.load()); }

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
    void setChorusVoices(int v) { chorusVoices_.store(std::clamp(v,1,3)); }
    void setChorusWidth(float v) { chorusWidth_.store(std::clamp(v,0.0f,1.0f)); }
    void setChorusFeedback(float v) { chorusFeedback_.store(std::clamp(v,-0.5f,0.5f)); }
    void setChorusTone(float v) { chorusTone_.store(std::clamp(v,500.0f,18000.0f)); }
    void setChorusDelay(float v) { chorusDelay_.store(std::clamp(v,7.0f,28.0f)); }

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

    void setReverbEnabled(bool en) { reverbEnabled_.store(en); }
    void setReverbType(int t) { reverbType_.store(std::clamp(t, 0, 3)); shimmerDirty_.store(true); }
    void setReverbMix(float m) { reverbMix_.store(std::clamp(m, 0.0f, 1.0f)); shimmerDirty_.store(true); }
    void setReverbSize(float sz) { reverbSize_.store(std::clamp(sz, 0.05f, 1.0f)); shimmerDirty_.store(true); }
    void setReverbDamping(float d) { reverbDamping_.store(std::clamp(d, 0.0f, 1.0f)); }
    void setReverbPreDelayMs(float ms) { reverbPreDelayMs_.store(std::clamp(ms, 0.0f, 240.0f)); shimmerDirty_.store(true); }
    void setReverbShimmer(float s) { reverbShimmer_.store(std::clamp(s, 0.0f, 1.0f)); }

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
    // Call from the non-real-time controller/HTTP worker. Audio callback only
    // enqueues retired PCM banks, never frees large sample allocations.
    void collectRetiredInstruments();
    // Lightweight, lock-free audio callback diagnostics. Neither allocates nor logs.
    void recordAudioCallback(uint64_t elapsedMicros,unsigned frames,bool underflow) noexcept {
        if(underflow)audioUnderflows_.fetch_add(1,std::memory_order_relaxed);
        const double budget=1.0e6*double(frames)/hostSampleRate_;
        if(double(elapsedMicros)>budget*.90)
            audioOverBudget_.fetch_add(1,std::memory_order_relaxed);
        auto maxTime=maxAudioRenderMicros_.load(std::memory_order_relaxed);
        while(maxTime<elapsedMicros && !maxAudioRenderMicros_.compare_exchange_weak(
            maxTime,elapsedMicros,std::memory_order_relaxed)){}
    }
    uint64_t getMidiDroppedEvents() const noexcept { return midiDropped_.load(std::memory_order_relaxed); }
    uint64_t getAudioUnderflows() const noexcept {return audioUnderflows_.load(std::memory_order_relaxed);}
    uint64_t getAudioOverBudgetCallbacks() const noexcept {return audioOverBudget_.load(std::memory_order_relaxed);}
    uint64_t getMaxAudioRenderMicros() const noexcept {return maxAudioRenderMicros_.load(std::memory_order_relaxed);}

    // En la sección public: de src/modules/NordPianoEngine.h
    void setFlangerEnabled(bool enabled)   { flangerEnabled_.store(enabled); }
    void setFlangerRate(float rate)        { flangerRate_.store(rate); }
    void setFlangerDepth(float depth)      { flangerDepth_.store(depth); }
    void setFlangerFeedback(float fb)      { flangerFeedback_.store(fb); }
    void setFlangerMix(float mix)          { flangerMix_.store(mix); }


    
    NordSynthModule multiSynth_;
    NordOrganModule organModule_;

    // v31: per-piano-layer insert chains (the Sound's own effects). Written from the
    // parameter setters, read on the audio thread (same contract as synth layer inserts).
    void setPianoLayerInserts(int layer, const InsertFxChain& chain) {
        if (layer >= 0 && layer < PIANO_LAYER_COUNT) pianoInsertFx_[size_t(layer)] = chain;
    }
    InsertFxChain& pianoLayerInserts(int layer) { return pianoInsertFx_[size_t(std::clamp(layer, 0, PIANO_LAYER_COUNT - 1))]; }
    /** Synth-only all-notes-off from a UI / HTTP thread (voice-mode changes): queued to
        the audio thread, voices fade in 12 ms; piano and organ keep playing. */
    void queueSynthAllNotesOff() { pushMidiEvent(5, 0, 0); }
    /** Master limiter gain reduction of the last callback, in dB (0 = not limiting). */
    float getLimiterReductionDb() const { return limiterReductionDb_.load(std::memory_order_relaxed); }

private:
    std::array<std::atomic<float>,3> modulePan_{};
    std::array<std::atomic<float>,3> moduleTune_{};
    void pushMidiEvent(uint8_t type, int d1, int d2);
    void handleNoteOnInternal(const std::array<std::shared_ptr<const PianoInstrument>, PIANO_LAYER_COUNT>& instruments, int rawMidiKey, int velocity);
    void startPianoVoice(const std::shared_ptr<const PianoInstrument>& inst, int layer, int rawMidiKey, int midiKey, int velocity,
                         const StrokeSample* selectedStroke=nullptr, bool replaceSameKey=true, float polyGain=1.f, bool releaseVoice=false);
    void handleNoteOffInternal(int rawMidiKey);
    void handleSustainInternal(bool down);

    const StrokeSample* selectStrokeFor(const PianoInstrument& inst, int midiKey, int velocity) const;
    int touchAttenuation(int velocity) const;

    double hostSampleRate_ = 48000.0;
    std::shared_ptr<const PianoInstrument> instrument_; // legacy layer 1
    std::array<std::shared_ptr<const PianoInstrument>, PIANO_LAYER_COUNT - 1> extraPianoInstruments_{};
    mutable std::mutex instPtrMutex_; // control threads only
    struct BankChange {int layer=-1;std::shared_ptr<const PianoInstrument> bank;};
    static constexpr size_t BANK_CHANGE_QUEUE_SIZE=256;
    std::array<BankChange,BANK_CHANGE_QUEUE_SIZE> bankChanges_{};
    std::atomic<size_t> bankChangeWrite_{0},bankChangeRead_{0};
    // Audio callback owns this separate bank snapshot; its replacements move
    // to the retirement queue, so no mutex or destructor runs in render().
    std::array<std::shared_ptr<const PianoInstrument>,PIANO_LAYER_COUNT> audioInstruments_{};

    // One producer (audio callback), one consumer (control worker), no locks or
    // allocations in render(). Keep the queue sufficiently larger than voices_.
    static constexpr size_t RETIRED_BANK_QUEUE_SIZE=4096;
    std::array<std::shared_ptr<const PianoInstrument>,RETIRED_BANK_QUEUE_SIZE> retiredBanks_{};
    std::atomic<size_t> retiredBankWrite_{0},retiredBankRead_{0};
    void retireBankFromAudio(std::shared_ptr<const PianoInstrument>& bank);

    std::atomic<bool> studioRoutingEnabled_{true};
    std::array<std::array<std::atomic<int>, STUDIO_SLOTS>, STUDIO_RACK_COUNT> studioSlotType_{};
    std::array<std::array<std::atomic<int>, STUDIO_SLOTS>, STUDIO_RACK_COUNT> studioSlotVersion_{};
    std::array<std::array<std::atomic<bool>, STUDIO_SLOTS>, STUDIO_RACK_COUNT> studioSlotEnabled_{};
    std::array<std::array<std::array<std::atomic<float>,STUDIO_PARAM_COUNT>, STUDIO_SLOTS>,STUDIO_RACK_COUNT> studioSlotParam_{};
    std::array<std::array<std::array<std::atomic<float>, STUDIO_SLOTS>, STUDIO_MAX_SOURCES>,STUDIO_RACK_COUNT> studioSends_{};
    std::array<std::array<std::array<float, STUDIO_SLOTS>, STUDIO_MAX_SOURCES>,STUDIO_RACK_COUNT> studioSendSmooth_{};
    std::array<std::atomic<bool>, STUDIO_RACK_COUNT> studioEqEnabled_{};
    std::array<std::array<std::atomic<float>,5>,STUDIO_RACK_COUNT> studioEqDb_{};
    std::array<std::array<std::atomic<float>,5>,STUDIO_RACK_COUNT> studioEqFreq_{},studioEqQ_{};
    std::array<std::atomic<float>,STUDIO_RACK_COUNT> studioEqHighPass_{},studioEqLowPass_{},studioEqInputDb_{},studioEqOutputDb_{};
    std::array<std::atomic<int>,STUDIO_RACK_COUNT> studioEqPosition_{};
    std::array<std::atomic<bool>,STUDIO_RACK_COUNT> studioEqDirty_{};
    std::array<StudioRackDSP,STUDIO_RACK_COUNT> studioRackDsp_{};
    std::array<std::atomic<int>,STUDIO_RACK_COUNT> studioEqHighSlope_{},studioEqLowSlope_{};
    // Slot processors. Ownership lives on the control threads (studioSlotDsp_, guarded
    // by studioSlotBuildMutex_); the audio thread only reads studioSlotRaw_ (lock-free).
    // An effect that is switched off or replaced keeps ringing (spill-over) on the audio
    // side; studioHeld_ tells the control thread which processors are still in use.
    std::array<std::array<std::shared_ptr<StudioSlotDSP>,STUDIO_SLOTS>,STUDIO_RACK_COUNT> studioSlotDsp_{};
    std::array<std::array<std::atomic<StudioSlotDSP*>,STUDIO_SLOTS>,STUDIO_RACK_COUNT> studioSlotRaw_{};
    std::array<std::array<std::array<std::atomic<StudioSlotDSP*>,2>,STUDIO_SLOTS>,STUDIO_RACK_COUNT> studioHeld_{};
    struct RetiredSlot { std::shared_ptr<StudioSlotDSP> dsp; uint64_t epoch = 0; };
    // Retire old processors on the CONTROL thread, never in the audio callback.
    std::vector<RetiredSlot> studioRetired_{};
    std::mutex studioSlotBuildMutex_{};
    AudioEpoch audioEpoch_;
    void prepareStudioSlot(int rack,int slot,int type);
    // Control thread, studioSlotBuildMutex_ held: swap a slot's processor / free retired ones.
    void publishStudioSlotLocked(int rack,int slot,std::shared_ptr<StudioSlotDSP> next);
    void collectStudioRetiredLocked();
    // Audio-thread lane of a rack slot: the processor in use, the parameters it runs
    // with, its input ramp and its remaining tail.
    struct SlotLane {
        StudioSlotDSP* dsp = nullptr;
        StudioSlotConfig cfg{};
        float input = 0.f;
        int tail = 0;
        int silent = 0;
    };
    std::array<std::array<SlotLane,STUDIO_SLOTS>,STUDIO_RACK_COUNT> slotLive_{}, slotSpill_{};
    std::array<float,STUDIO_RACK_COUNT> rackEqGate_{};
    std::array<float,PIANO_LAYER_COUNT> soundEqGate_{};
    // Master output: smoothed gain / pan and a zero-latency peak limiter.
    float masterGainSmoothL_ = -1.f, masterGainSmoothR_ = -1.f;
    float limiterGain_ = 1.f;      // applied gain (attack-smoothed)
    float limiterHold_ = 1.f;      // held target gain
    int limiterHoldCount_ = 0;
    std::atomic<float> limiterReductionDb_{0.f};
    int lastPianoVoices_ = 0;
    float bendBlockStart_ = 0.f;   // pitch bend at the start of the next block
    std::atomic<float> aftertouch_{0.f};
    std::array<std::array<std::atomic<float>, FX_BUS_COUNT>, FX_SOURCE_COUNT> fxSends_{};
    std::array<std::array<float, FX_BUS_COUNT>, FX_SOURCE_COUNT> fxSendSmoothed_{};
    std::array<std::vector<float>, FX_SOURCE_COUNT> sourceAudio_{};
    std::array<std::array<std::atomic<float>,3>,3> synthFxSend_{};
    float synthFxSendSmoothed_[3][3]{}; // audio-thread only; dezipper on AUX transitions
    std::array<std::atomic<float>,3> organFxSend_{};
    float organFxSendSmoothed_[3]{1.f,1.f,1.f};
    std::array<std::array<std::atomic<float>,30>,2> moduleFxParams_{};
    NordModuleFxRack synthModuleFx_, organModuleFx_;
    NordColorEffects masterColorFx_;
    NordFlangerEffect masterFlangerFx_;
    std::atomic<bool> masterDriveOn_{false},masterAutoPanOn_{false};
    std::atomic<float> masterDriveAmt_{.25f},masterAutoPanRate_{1.5f},masterAutoPanDepth_{.45f};
    std::array<std::atomic<int>,3> moduleZoneLow_{{0,0,0}},moduleZoneHigh_{{127,127,127}};
    std::array<std::atomic<int>,PIANO_LAYER_COUNT> pianoZoneLow_{{0,0,0,0,0}},pianoZoneHigh_{{127,127,127,127,127}};

    std::array<Voice, MAX_VOICES> voices_;
    struct StealSmoothing {
        int layer=0;
        int total=1;
        int remaining=0;
        float left=0.f,right=0.f;
    };
    std::array<StealSmoothing,MAX_VOICES> stealSmoothing_{};
    bool sustainPedalDown_ = false;
    uint64_t voiceCounter_ = 0;

    // Bounded MPSC queue. Producers: RtMidi and HTTP/GUI. Consumer: audio callback.
    // Sequence numbers give each slot an exclusive owner before it is published.
    struct MidiQueueSlot {
        std::atomic<size_t> sequence{0};
        QueuedMidiEvent event{};
    };
    std::array<MidiQueueSlot, MIDI_QUEUE_SIZE> midiQueue_{};
    std::atomic<size_t> midiWriteIdx_{0};
    std::atomic<size_t> midiReadIdx_{0};
    std::atomic<uint64_t> midiDropped_{0};
    // If a bounded queue overflows, do not lose safety-critical key releases.
    std::array<std::atomic<uint64_t>,2> emergencyNoteOffMasks_{};
    std::atomic<bool> emergencyAllNotesOff_{false};
    std::atomic<bool> emergencySustainOff_{false};

    std::atomic<uint32_t> testToneFramesRemaining_{0};
    double testTonePhase_ = 0.0;

    std::atomic<bool> pianoEnabled_{true};
    std::atomic<uint64_t> audioUnderflows_{0},audioOverBudget_{0},maxAudioRenderMicros_{0};
    std::atomic<float> pianoVolume_{1.0f};
    std::array<std::atomic<bool>, PIANO_LAYER_COUNT> pianoLayerEnabled_{{true, false, false, false, false}};
    std::array<std::atomic<float>, PIANO_LAYER_COUNT> pianoLayerVolume_{{1.0f, 0.75f, 0.75f, 0.75f, 0.75f}};
    // Audio-thread-only, smoothed layer gain including master level and bypass.
    std::array<float,PIANO_LAYER_COUNT> pianoLayerGainSmooth_{{1.f,0.f,0.f,0.f,0.f}};
    std::array<std::array<std::atomic<float>, 10>, PIANO_LAYER_COUNT> pianoLayerSample_{};
    struct LayerFilterState { float ic1L=0.f,ic2L=0.f,ic1R=0.f,ic2R=0.f; };
    std::array<LayerFilterState, PIANO_LAYER_COUNT> pianoFilterStates_{};
    std::array<std::atomic<bool>,PIANO_LAYER_COUNT> soundEqEnabled_{},soundEqDirty_{};
    std::array<std::array<std::atomic<float>,3>,PIANO_LAYER_COUNT> soundEqFreq_{},soundEqQ_{},soundEqGain_{};
    std::array<std::atomic<float>,PIANO_LAYER_COUNT> soundEqHighPass_{},soundEqLowPass_{};
    std::array<std::atomic<int>,PIANO_LAYER_COUNT> soundEqHighSlope_{},soundEqLowSlope_{};
    std::array<SoundLayerEqualizer,PIANO_LAYER_COUNT> soundLayerEq_{};
    std::array<InsertFxChain,PIANO_LAYER_COUNT> pianoInsertFx_{};
    std::array<SynthLayerInsertDSP,PIANO_LAYER_COUNT> pianoInsertDSP_{};
    std::array<bool,PIANO_LAYER_COUNT> pianoInsertTail_{};
    std::array<unsigned,PIANO_LAYER_COUNT> pianoInsertSilent_{};
    std::array<std::atomic<float>, PIANO_LAYER_COUNT> pianoWheelCutoff_{};
    std::array<std::atomic<float>, PIANO_LAYER_COUNT> pianoWheelAmpDb_{};
    std::array<std::atomic<bool>, PIANO_LAYER_COUNT> pianoWheelCutoffEnabled_{};
    std::array<std::atomic<bool>, PIANO_LAYER_COUNT> pianoWheelCutoffInvert_{};
    std::array<std::atomic<bool>, PIANO_LAYER_COUNT> pianoWheelAmpEnabled_{};
    std::array<std::atomic<bool>, PIANO_LAYER_COUNT> pianoWheelVibratoEnabled_{};
    std::array<std::atomic<float>, PIANO_LAYER_COUNT> pianoWheelVibratoCents_{};
    std::array<std::atomic<float>, PIANO_LAYER_COUNT> pianoWheelVibratoRate_{};
    std::array<std::atomic<bool>, PIANO_LAYER_COUNT> pianoMonoMode_{};
    std::array<std::atomic<bool>, PIANO_LAYER_COUNT> pianoLegatoEnabled_{};
    std::atomic<float> modWheel_{0.0f};
    std::atomic<float> pitchBend_{0.0f};
    // Audio-thread-only physical key tracking for mono Piano/Sample layers.
    // A sounding voice changes its midiKey during legato, therefore Voice::keyHeld
    // cannot tell us which *other* physical keys remain depressed. Bounded arrays
    // are essential here: no containers, mutexes or allocations in render().
    struct MonoHeldKeys {
        std::array<uint8_t,128> down{};
        std::array<uint8_t,128> velocity{};
        std::array<uint64_t,128> order{};
        uint64_t clock = 0;
        int soundingKey = -1;
        void clear() noexcept { down.fill(0); velocity.fill(0); order.fill(0); clock=0; soundingKey=-1; }
        void press(int key,int vel) noexcept {
            down[size_t(key)]=1;
            velocity[size_t(key)]=static_cast<uint8_t>(std::clamp(vel,1,127));
            order[size_t(key)]=++clock;
        }
        void release(int key) noexcept { down[size_t(key)]=0; }
        int mostRecent() const noexcept {
            int best=-1;uint64_t latest=0;
            for(int k=0;k<128;++k)if(down[size_t(k)] && order[size_t(k)]>latest){
                latest=order[size_t(k)];best=k;
            }
            return best;
        }
    };
    std::array<MonoHeldKeys,PIANO_LAYER_COUNT> monoHeldKeys_{};
    std::array<int, PIANO_LAYER_COUNT> lastSampleMidi_{{-1,-1,-1,-1,-1}};
    std::array<std::atomic<bool>, PIANO_LAYER_COUNT> sampleGlideOn_{};
    std::array<std::atomic<float>, PIANO_LAYER_COUNT> sampleGlideTime_{};
    std::array<std::atomic<int>, PIANO_LAYER_COUNT> sampleGlideMode_{};
    std::atomic<int> maxVoicesLimit_{128};
    // Mutable exclusively on the audio callback thread.
    int pianoVoiceBudget_ = 128;
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
    std::atomic<int> chorusVoices_{2};
    std::atomic<float> chorusWidth_{0.85f};
    std::atomic<float> chorusFeedback_{0.08f};
    std::atomic<float> chorusTone_{9500.0f};
    std::atomic<float> chorusDelay_{16.0f};

    std::atomic<bool> phaserEnabled_{false};
    std::atomic<float> phaserRate_{0.6f};
    std::atomic<float> phaserDepth_{0.60f};
    std::atomic<float> phaserFeedback_{0.40f};
    std::atomic<float> phaserMix_{0.35f};

    std::atomic<bool> flangerEnabled_{false};
    std::atomic<float> flangerRate_{.35f},flangerDepth_{.55f},flangerFeedback_{.25f},flangerMix_{.35f};

    std::atomic<bool> tremoloEnabled_{false};
    std::atomic<float> tremoloRate_{4.5f};
    std::atomic<float> tremoloDepth_{0.50f};

    std::atomic<bool> delayEnabled_{false};
    std::atomic<float> delayTimeMs_{360.0f};
    std::atomic<float> masterBpm_{120.0f};
    std::atomic<bool> delayTempoSync_{false};
    std::atomic<int> delayDivision_{5};
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
    // Audio-thread-owned counters: deterministic note alternation, no heap allocation.
    std::array<uint32_t,128> sampleRepeatCounter_{};
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