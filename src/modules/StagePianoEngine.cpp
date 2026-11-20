#include "../dsp/StageTempo.h"
#include "StagePianoEngine.h"
#include <cmath>
#include <algorithm>
#include <thread>

namespace SonusDSP {

StagePianoEngine::StagePianoEngine() {
    for (size_t i = 0; i < MIDI_QUEUE_SIZE; ++i)
        midiQueue_[i].sequence.store(i, std::memory_order_relaxed);
    for (auto& mask: emergencyNoteOffMasks_) mask.store(0, std::memory_order_relaxed);
    const float defaultFreqs[5] = {60.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f};
    const float defaultQs[5]    = {0.9f,  1.0f,   1.0f,    1.0f,    0.9f};
    for (int i = 0; i < 5; ++i) {
        eqFreqHz_[i].store(defaultFreqs[i]);
        eqQ_[i].store(defaultQs[i]);
        eqGainDb_[i].store(0.0f);
    }
    for (auto& ks : keyStates_) ks.store(0);
    for (int i = 0; i < PIANO_LAYER_COUNT; ++i) {
        const float defaults[10] = {0.005f, 3.5f, 1.f, 0.35f, 0.65f, 0.f, 0.f, 20000.f, .707f,0.f};
        for (int k = 0; k < 10; ++k) pianoLayerSample_[i][k].store(defaults[k]);
        const PianoLayerSampleConfig eqDefaults{};
        soundEqEnabled_[i].store(eqDefaults.soundEqEnabled);
        soundEqHighPass_[i].store(eqDefaults.soundEqHighPass);
        soundEqLowPass_[i].store(eqDefaults.soundEqLowPass);
        soundEqHighSlope_[i].store(12);soundEqLowSlope_[i].store(12);
        for(int b=0;b<3;++b){soundEqFreq_[i][b].store(eqDefaults.soundEqFreq[b]);
            soundEqQ_[i][b].store(eqDefaults.soundEqQ[b]);soundEqGain_[i][b].store(0.f);}
        soundEqDirty_[i].store(true);
    }
    for (int src = 0; src < FX_SOURCE_COUNT; ++src) {
        for (int bus = 0; bus < FX_BUS_COUNT; ++bus) {
            fxSends_[src][bus].store(1.0f);
            fxSendSmoothed_[src][bus] = 1.0f;
        }
    }
    multiSynth_.init(48000.0f);
    organModule_.init(48000.0f);
    const ModuleFxConfig defaults;
    const float vals[30]={float(defaults.chorusEnabled),defaults.chorusRate,defaults.chorusDepth,defaults.chorusMix,
       float(defaults.delayEnabled),defaults.delayMs,defaults.delayFeedback,defaults.delayMix,
       float(defaults.reverbEnabled),float(defaults.reverbType),defaults.reverbMix,defaults.reverbSize,
       float(defaults.driveEnabled),defaults.driveAmount,float(defaults.autoPanEnabled),defaults.autoPanRate,defaults.autoPanDepth,
       float(defaults.phaserEnabled),defaults.phaserRate,defaults.phaserDepth,defaults.phaserFeedback,defaults.phaserMix,
       float(defaults.tremoloEnabled),defaults.tremoloRate,defaults.tremoloDepth,
       float(defaults.flangerEnabled),defaults.flangerRate,defaults.flangerDepth,defaults.flangerFeedback,defaults.flangerMix};
    for(auto& fx:moduleFxParams_)for(int i=0;i<30;i++)fx[i].store(vals[i]);
    for(auto& layer:synthFxSend_)for(auto& send:layer)send.store(0.f);
    for(auto& send:organFxSend_)send.store(1.f);
    for(int rack=0;rack<STUDIO_RACK_COUNT;++rack){
        studioEqEnabled_[rack].store(true);
        studioEqDirty_[rack].store(true);
        studioEqHighSlope_[rack].store(12);studioEqLowSlope_[rack].store(12);
        const StudioRackConfig eqDefaults{};
        for(int band=0;band<5;++band){studioEqDb_[rack][band].store(0.f);studioEqFreq_[rack][band].store(eqDefaults.eqFreq[band]);studioEqQ_[rack][band].store(eqDefaults.eqQ[band]);}
        studioEqHighPass_[rack].store(20.f);studioEqLowPass_[rack].store(20000.f);
        studioEqInputDb_[rack].store(0.f);studioEqOutputDb_[rack].store(0.f);studioEqPosition_[rack].store(0);
        for(int slot=0;slot<STUDIO_SLOTS;++slot){
            studioSlotType_[rack][slot].store(0);
            studioSlotVersion_[rack][slot].store(1);
            studioSlotEnabled_[rack][slot].store(false);
            const StudioSlotConfig initial{};
            for(int k=0;k<STUDIO_PARAM_COUNT;k++)studioSlotParam_[rack][slot][k].store(initial.p[k]);
            for(int source=0;source<STUDIO_MAX_SOURCES;++source){
                const float start=(rack==3&&slot==0)?0.22f:0.f;
                studioSends_[rack][source][slot].store(start);
                studioSendSmooth_[rack][source][slot]=start;
            }
        }
    }
    // New projects have one shared acoustic reverb; every local rack starts clean.
    studioSlotType_[3][0].store(6);
    studioSlotEnabled_[3][0].store(true);
    prepareStudioSlot(3,0,6);
    filterDirty_.store(true);
    eqDirty_.store(true);
    for(int layer=0;layer<PIANO_LAYER_COUNT;++layer)soundEqDirty_[layer].store(true);
    shimmerDirty_.store(true);
    // RtAudio puede llamar al callback inmediatamente al arrancar. Los stems
    // no pueden permanecer vacios hasta que HardwareManager configure el audio.
    // El dispositivo ajustara la frecuencia real ANTES de startStream().
    setSampleRate(48000.0);
}

void StagePianoEngine::setSampleRate(double sr) {
    hostSampleRate_ = (sr > 8000.0) ? sr : 48000.0;
    float fsr = static_cast<float>(hostSampleRate_);
    multiSynth_.init(fsr);
    organModule_.init(fsr);
    // Preallocate up to 8192 stereo frames per source outside the real-time callback.
    // The buffers are reused for subsequent render blocks without reallocations.
    for (auto& stem : sourceAudio_) stem.assign(16384, 0.0f);
    for (auto& chain : pianoInsertDSP_) chain.init(fsr);
    pianoInsertTail_.fill(false); pianoInsertSilent_.fill(0);
    svfFilter_.reset();
    for(int r=0;r<STUDIO_RACK_COUNT;++r){
        studioEqDirty_[r].store(true);
        for(int k=0;k<STUDIO_SLOTS;++k){
            int type=studioSlotType_[r][k].load();
            if(type&&studioSlotEnabled_[r][k].load()){
                // Sample-rate changes rebuild delay lines on the control thread.
                {
                    std::lock_guard<std::mutex> guard(studioSlotBuildMutex_);
                    publishStudioSlotLocked(r,k,{});
                }
                prepareStudioSlot(r,k,type);
            }
        }
    }
    filterDirty_.store(true);
    eqDirty_.store(true);
    shimmerDirty_.store(true);
}

void StagePianoEngine::setInstrument(std::shared_ptr<const PianoInstrument> inst) {
    setLayerInstrument(0, std::move(inst));
}

void StagePianoEngine::setLayerInstrument(int layer, std::shared_ptr<const PianoInstrument> inst) {
    if(layer<0 || layer>=PIANO_LAYER_COUNT)return;
    // Control-only producer: serializes multiple REST/plugin callers, but is
    // never taken by render(). The SPSC ring has no heap allocations.
    std::lock_guard<std::mutex> lock(instPtrMutex_);
    if(layer==0)instrument_=inst;
    else extraPianoInstruments_[layer-1]=inst;
    const size_t write=bankChangeWrite_.load(std::memory_order_relaxed);
    size_t read=bankChangeRead_.load(std::memory_order_acquire);
    // In practice the callback consumes five updates in its next buffer. If
    // congested, wait only on the control thread; never clobber unread slots.
    while((write+1)%BANK_CHANGE_QUEUE_SIZE==read){
        std::this_thread::yield();
        read=bankChangeRead_.load(std::memory_order_acquire);
    }
    bankChanges_[write].layer=layer;
    bankChanges_[write].bank=std::move(inst);
    bankChangeWrite_.store((write+1)%BANK_CHANGE_QUEUE_SIZE,std::memory_order_release);
    lastTriggeredStrokeIdx_.store(0);
}

void StagePianoEngine::setInstrument(const PianoInstrument* inst) {
    if (!inst) {
        setInstrument(std::shared_ptr<const PianoInstrument>(nullptr));
        return;
    }
    // Legacy caller-owned pointer (must outlive use). New layer API uses owning pointers.
    std::shared_ptr<const PianoInstrument> aliasPtr(inst, [](const PianoInstrument*) {});
    setInstrument(std::move(aliasPtr));
}

std::shared_ptr<const PianoInstrument> StagePianoEngine::getInstrument() const {
    return getLayerInstrument(0);
}

std::shared_ptr<const PianoInstrument> StagePianoEngine::getLayerInstrument(int layer) const {
    if (layer < 0 || layer >= PIANO_LAYER_COUNT) return {};
    std::lock_guard<std::mutex> lock(instPtrMutex_);
    return layer == 0 ? instrument_ : extraPianoInstruments_[layer - 1];
}

std::array<std::shared_ptr<const PianoInstrument>, PIANO_LAYER_COUNT> StagePianoEngine::getPianoInstruments() const {
    std::lock_guard<std::mutex> lock(instPtrMutex_);
    return {instrument_, extraPianoInstruments_[0], extraPianoInstruments_[1],
            extraPianoInstruments_[2], extraPianoInstruments_[3]};
}

void StagePianoEngine::retireBankFromAudio(std::shared_ptr<const PianoInstrument>& bank){
    if(!bank)return;
    const size_t write=retiredBankWrite_.load(std::memory_order_relaxed);
    const size_t next=(write+1)%RETIRED_BANK_QUEUE_SIZE;
    if(next==retiredBankRead_.load(std::memory_order_acquire))return;
    // The consumer has already cleared this ring cell before advancing read.
    retiredBanks_[write]=std::move(bank);
    retiredBankWrite_.store(next,std::memory_order_release);
}

void StagePianoEngine::collectRetiredInstruments(){
    {
        std::lock_guard<std::mutex> lock(studioSlotBuildMutex_);
        collectStudioRetiredLocked();
    }
    size_t read=retiredBankRead_.load(std::memory_order_relaxed);
    const size_t write=retiredBankWrite_.load(std::memory_order_acquire);
    while(read!=write){
        // Destructor of an uncached multi-megabyte instrument runs on the
        // control thread, never in the low-latency ASIO audio callback.
        auto old=std::move(retiredBanks_[read]);
        read=(read+1)%RETIRED_BANK_QUEUE_SIZE;
        retiredBankRead_.store(read,std::memory_order_release);
        old.reset();
    }
}

void StagePianoEngine::setPianoLayerSampleParam(int layer, int param, float value) {
    if (layer < 0 || layer >= PIANO_LAYER_COUNT || param < 0 || param >= 10) return;
    const float lo[10] = {0.001f,0.02f,0.f,0.02f,0.f,-1.f,-100.f,40.f,0.5f,-36.f};   // v32.2: 1 ms attack keeps the hammer transient
    const float hi[10] = {5.f,12.f,1.f,10.f,1.f,1.f,100.f,20000.f,12.f,18.f};
    pianoLayerSample_[layer][param].store(std::clamp(value, lo[param], hi[param]));
}

void StagePianoEngine::setSoundEqParameter(int layer,int field,float value){
    if(layer<0||layer>=PIANO_LAYER_COUNT||field<0||field>13||!std::isfinite(value))return;
    switch(field){
      case 0:soundEqEnabled_[layer].store(value>=.5f);break;
      case 1:soundEqHighPass_[layer].store(std::clamp(value,20.f,1000.f));break;
      case 2:soundEqLowPass_[layer].store(std::clamp(value,1000.f,20000.f));break;
      case 3:soundEqHighSlope_[layer].store(normalizeEqSlope(int(std::lround(value))));break;
      case 4:soundEqLowSlope_[layer].store(normalizeEqSlope(int(std::lround(value))));break;
      default:{const int idx=(field-5)/3,attr=(field-5)%3;
        if(idx>=3)return;
        if(attr==0)soundEqFreq_[layer][idx].store(std::clamp(value,30.f,16000.f));
        if(attr==1)soundEqQ_[layer][idx].store(std::clamp(value,.3f,12.f));
        if(attr==2)soundEqGain_[layer][idx].store(std::clamp(value,-15.f,15.f));
      }break;
    }
    soundEqDirty_[layer].store(true,std::memory_order_release);
}
/** Synth part of a full preset / program. Whole-patch write: faded out around it. */
void StagePianoEngine::applySynthParams(const SynthPresetParams& p) {
    PatchChangeGuard hold(multiSynth_);
    multiSynth_.synthEnabled = p.synthEnabled;
    multiSynth_.synthPresetId = p.synthPresetId;
    multiSynth_.synthMasterVol = p.synthMasterVol;
    multiSynth_.synthMacroCutoff = p.synthMacroCutoff;
    multiSynth_.synthWheelGlobalCutoff = p.synthWheelGlobalCutoff;
    multiSynth_.synthWheelGlobalAmpDb = p.synthWheelGlobalAmpDb;
    multiSynth_.synthOutGainDb = p.synthOutGainDb;
    multiSynth_.synthVoiceMaster=p.synthVoiceMaster;
    multiSynth_.synthVoiceMode = p.synthVoiceMode;
    multiSynth_.synthSeqGlobalClock = p.synthSeqGlobalClock;
    multiSynth_.synthSeqSharePattern = p.synthSeqSharePattern;
    multiSynth_.macros = p.synthMacros;
    multiSynth_.setModMatrix(p.modMatrix);
    multiSynth_.synthPortamento = p.synthPortamento;
    multiSynth_.synthPortamentoEnabled = p.synthPortamentoEnabled;
    multiSynth_.synthGlideMode = p.synthGlideMode;
    multiSynth_.synthSlide = p.synthSlide;
    for (int i = 0; i < SYNTH_LAYER_COUNT; ++i) {
        multiSynth_.layers[i].cfg = p.synthLayers[i];
    }
    for (int i = 0; i < SYNTH_NUM_LFOS; ++i) {
        const auto& src = p.synthLfos[i];
        auto& dst = multiSynth_.lfos[i];
        dst.enabled = src.enabled;
        dst.waveType = src.waveType;
        dst.rateHz = src.rateHz;
        dst.depth = src.depth;
        dst.delaySec = src.delaySec;
        dst.fadeSec = src.fadeSec;
        dst.destPitch = src.destPitch;
        dst.destCutoff = src.destCutoff;
        dst.destVolume = src.destVolume;
        dst.destPan = src.destPan;
        dst.destPhase = src.destPhase;
        dst.keySync = src.keySync;
    }
    for (int i = 0; i < SYNTH_NUM_ENVS; ++i) {
        const auto& src = p.synthEnvelopes[i];
        auto& dst = multiSynth_.envelopes[i];
        dst.delaySec = src.delaySec;
        dst.attackSec = src.attackSec;
        dst.holdSec = src.holdSec;
        dst.decaySec = src.decaySec;
        dst.sustainLevel = src.sustainLevel;
        dst.releaseSec = src.releaseSec;
    }
    // Explicit UI alias remains authoritative for LFO #1 and legacy program files.
    multiSynth_.lfos[0].enabled = p.synthLfoEnabled;
    multiSynth_.lfos[0].waveType = p.synthLfoWave;
    multiSynth_.lfos[0].rateHz = p.synthLfoRate;
    multiSynth_.lfos[0].depth = p.synthLfoDepth;
    multiSynth_.lfos[0].destPitch = p.synthLfoPitch;
    multiSynth_.lfos[0].destCutoff = p.synthLfoCutoff;

}

void StagePianoEngine::applyPreset(const SynthPresetParams& p) {
    setMasterBpm(p.masterBpm);setDelayTempoSync(p.delayTempoSync);setDelayDivision(p.delayDivision);
    setStudioRoutingEnabled(p.studioRoutingEnabled);
    for(int r=0;r<STUDIO_RACK_COUNT;++r)setStudioRack(r,p.studioRacks[r]);
    setPianoEnabled(p.pianoEnabled);
    setPianoVolume(p.pianoVolume);
    setModulePan(0,p.pianoPan);setModulePan(1,p.synthPan);setModulePan(2,p.organPan);
    setModuleTune(0,p.pianoTune);setModuleTune(1,p.synthTune);setModuleTune(2,p.organTune);
    for (int i = 0; i < PIANO_LAYER_COUNT; ++i) {
        pianoWheelCutoff_[i].store(p.pianoLayerSample[i].wheelCutoffOctaves);
        pianoWheelAmpDb_[i].store(p.pianoLayerSample[i].wheelAmpDb);
        pianoInsertFx_[size_t(i)] = p.pianoLayerSample[i].insertFx;
        pianoWheelCutoffEnabled_[i].store(p.pianoLayerSample[i].wheelCutoffEnabled);
        pianoWheelCutoffInvert_[i].store(p.pianoLayerSample[i].wheelCutoffInvert);
        pianoWheelAmpEnabled_[i].store(p.pianoLayerSample[i].wheelAmpEnabled);
        pianoWheelVibratoEnabled_[i].store(p.pianoLayerSample[i].wheelVibratoEnabled);
        pianoWheelVibratoCents_[i].store(p.pianoLayerSample[i].wheelVibratoCents);
        pianoWheelVibratoRate_[i].store(p.pianoLayerSample[i].wheelVibratoRateHz);
        pianoMonoMode_[i].store(p.pianoLayerSample[i].monoMode);
        pianoLegatoEnabled_[i].store(p.pianoLayerSample[i].legatoEnabled);
        sampleGlideOn_[i].store(p.pianoLayerSample[i].portamentoEnabled);
        sampleGlideTime_[i].store(std::clamp(p.pianoLayerSample[i].portamentoSec,0.f,2.f));
        sampleGlideMode_[i].store(std::clamp(p.pianoLayerSample[i].portamentoMode,0,1));
        const auto& eq=p.pianoLayerSample[i];
        setSoundEqParameter(i,0,eq.soundEqEnabled?1.f:0.f);
        setSoundEqParameter(i,1,eq.soundEqHighPass);
        setSoundEqParameter(i,2,eq.soundEqLowPass);
        setSoundEqParameter(i,3,float(eq.soundEqHighPassSlope));
        setSoundEqParameter(i,4,float(eq.soundEqLowPassSlope));
        for(int b=0;b<3;++b){setSoundEqParameter(i,5+b*3,eq.soundEqFreq[b]);
            setSoundEqParameter(i,6+b*3,eq.soundEqQ[b]);
            setSoundEqParameter(i,7+b*3,eq.soundEqGain[b]);}
        setPianoLayerEnabled(i, p.pianoLayerEnabled[i]);
        setPianoLayerVolume(i, p.pianoLayerVolume[i]);
    }
    for (int src = 0; src < FX_SOURCE_COUNT; ++src) {
        setFxSend(src, 0, p.fxSends[src].modulation);
        setFxSend(src, 1, p.fxSends[src].delay);
        setFxSend(src, 2, p.fxSends[src].reverb);
    }
    for(int module=0;module<3;module++) {
        const KeyZone& z=(module==0?p.pianoZone:module==1?p.synthZone:p.organZone);
        setModuleZone(module,false,z.low);setModuleZone(module,true,z.high);
    }
    for(int layer=0;layer<PIANO_LAYER_COUNT;layer++){
        setPianoZone(layer,false,p.pianoLayerZones[layer].low);setPianoZone(layer,true,p.pianoLayerZones[layer].high);
        setPianoLayerOctave(layer,p.pianoLayerZones[layer].octave);setPianoLayerSemi(layer,p.pianoLayerZones[layer].transpose);
    }
    const auto storeFx=[this](bool synth,const ModuleFxConfig& c){
        const float values[30]={float(c.chorusEnabled),c.chorusRate,c.chorusDepth,c.chorusMix,float(c.delayEnabled),c.delayMs,c.delayFeedback,c.delayMix,
            float(c.reverbEnabled),float(c.reverbType),c.reverbMix,c.reverbSize,float(c.driveEnabled),c.driveAmount,float(c.autoPanEnabled),c.autoPanRate,c.autoPanDepth,
            float(c.phaserEnabled),c.phaserRate,c.phaserDepth,c.phaserFeedback,c.phaserMix,
            float(c.tremoloEnabled),c.tremoloRate,c.tremoloDepth,
            float(c.flangerEnabled),c.flangerRate,c.flangerDepth,c.flangerFeedback,c.flangerMix};
        for(int i=0;i<30;i++)setModuleFxParam(synth,i,values[i]);
    };
    storeFx(true,p.synthFx);storeFx(false,p.organFx);
    for(int i=0;i<3;i++){
        setModuleFxSend(i,0,p.synthFxSends[i].modulation);
        setModuleFxSend(i,1,p.synthFxSends[i].delay);
        setModuleFxSend(i,2,p.synthFxSends[i].reverb);
    }
    setOrganFxSend(0,p.organFxSends.modulation);
    setOrganFxSend(1,p.organFxSends.delay);
    setOrganFxSend(2,p.organFxSends.reverb);
    flangerEnabled_.store(p.flangerEnabled);flangerRate_.store(p.flangerRate);flangerDepth_.store(p.flangerDepth);
    flangerFeedback_.store(p.flangerFeedback);flangerMix_.store(p.flangerMix);
    masterDriveOn_.store(p.masterDriveEnabled);masterDriveAmt_.store(p.masterDriveAmount);
    masterAutoPanOn_.store(p.masterAutoPanEnabled);masterAutoPanRate_.store(p.masterAutoPanRate);masterAutoPanDepth_.store(p.masterAutoPanDepth);
    setMaxVoices(p.maxVoices);
    setTranspose(p.transposeSemis);
    setOctaveShift(p.octaveShift);
    setMasterTuneCents(p.masterTuneCents);
    setSoftRelease(p.softRelease);
    setKbdTouch(static_cast<KbdTouch>(std::clamp(p.kbdTouch, 0, 2)));

    setCutoffHz(p.cutoffHz);
    setResonanceQ(p.resonanceQ);
    setAttackSec(p.attackSec);
    setDecaySec(p.decaySec);
    setSustainLevel(p.sustainLevel);
    setReleaseSec(p.releaseSec);
    setLoopHold(p.loopHold);
    // Apply individual sound-layer envelopes *after* legacy global controls.
    for (int layer = 0; layer < PIANO_LAYER_COUNT; ++layer) {
        const auto& cfg = p.pianoLayerSample[layer];
        const float values[10] = {cfg.attackSec,cfg.decaySec,cfg.sustainLevel,cfg.releaseSec,
                                 cfg.loopHold,cfg.pan,cfg.fineTuneCents,cfg.cutoffHz,cfg.resonanceQ,cfg.gainDb};
        for (int k = 0; k < 10; ++k) setPianoLayerSampleParam(layer,k,values[k]);
    }
    setMasterGain(p.volume);
    setPan(p.pan);

    setEqEnabled(p.eqEnabled);
    setLowCutHz(p.lowCutHz);
    setHighCutHz(p.highCutHz);
    for (int i = 0; i < 5; ++i) {
        setEqBandFreqHz(i, p.eqBands[i].freqHz);
        setEqBandQ(i, p.eqBands[i].q);
        setEqBandGainDb(i, p.eqBands[i].gainDb);
    }

    setChorusEnabled(p.chorusEnabled);
    setChorusRate(p.chorusRateHz);
    setChorusDepth(p.chorusDepth);
    setChorusMix(p.chorusMix);
    setChorusVoices(p.chorusVoices);
    setChorusWidth(p.chorusWidth);
    setChorusFeedback(p.chorusFeedback);
    setChorusTone(p.chorusTone);
    setChorusDelay(p.chorusDelay);

    setPhaserEnabled(p.phaserEnabled);
    setPhaserRate(p.phaserRateHz);
    setPhaserDepth(p.phaserDepth);
    setPhaserFeedback(p.phaserFeedback);
    setPhaserMix(p.phaserMix);

    setTremoloEnabled(p.tremoloEnabled);
    setTremoloRate(p.tremoloRateHz);
    setTremoloDepth(p.tremoloDepth);

    setDelayEnabled(p.delayEnabled);
    setDelayTimeMs(p.delayTimeMs);
    setDelayFeedback(p.delayFeedback);
    setDelayDamping(p.delayDamping);
    setDelayMix(p.delayMix);
    setDelayPingPong(p.delayPingPong);

    setReverbEnabled(p.reverbEnabled);
    setReverbType(p.reverbType);
    setReverbMix(p.reverbMix);
    setReverbSize(p.reverbSize);
    setReverbDamping(p.reverbDamping);
    setReverbPreDelayMs(p.reverbPreDelayMs);
    setReverbShimmer(p.reverbShimmer);

    setShimmerShiftSt(p.shimmerShiftSt);
    setShimmerFeedback(p.shimmerFeedback);
    setShimmerDiffusion(p.shimmerDiffusion);
    setShimmerLowCutHz(p.shimmerLowCutHz);
    setShimmerHighCutHz(p.shimmerHighCutHz);
    setShimmerModRateHz(p.shimmerModRateHz);
    setShimmerModDepth(p.shimmerModDepth);
    setShimmerPitchMode(p.shimmerPitchMode);
    setShimmerReverbMode(p.shimmerReverbMode);
    setShimmerColorMode(p.shimmerColorMode);

    applySynthParams(p);

    organModule_.organEnabled = p.organEnabled;
    organModule_.organModel = p.organModel;
    organModule_.organVolume = p.organVolume;
    organModule_.organRotaryFast = p.organRotaryFast;
    organModule_.organDrive = p.organDrive;
    organModule_.rotaryMix = p.organRotaryMix;
    organModule_.rotaryFastRate=p.organRotaryFastRate;
    organModule_.rotaryRamp=p.organRotaryRamp;
    organModule_.rotaryStereoWidth=p.organRotaryStereoWidth;
    organModule_.vibratoDepth = p.organVibratoDepth;
    organModule_.vibratoRate = p.organVibratoRate;
    organModule_.chorusEnabled = p.organChorusEnabled;
    organModule_.chorusRate = p.organChorusRate;
    organModule_.chorusDepth = p.organChorusDepth;
    organModule_.chorusMix = p.organChorusMix;
    organModule_.chorusWidth = p.organChorusWidth;
    organModule_.chorusFeedback = p.organChorusFeedback;
    organModule_.chorusTone = p.organChorusTone;
    organModule_.chorusVoices = p.organChorusVoices;
    organModule_.vibratoMode = p.organVibratoMode;
    organModule_.percussionEnabled = p.organPercEnabled;
    organModule_.percussionHarmonic = p.organPercHarmonic;
    organModule_.percussionDecaySec = p.organPercDecaySec;
    organModule_.percussionLevel = p.organPercLevel;
    for (int d = 0; d < 9; ++d) {
        organModule_.drawbars[d] = p.drawbars[d];
    }
}

void StagePianoEngine::prepareStudioSlot(int rack,int slot,int type){
    if(rack<0||rack>=STUDIO_RACK_COUNT||slot<0||slot>=STUDIO_SLOTS||type<1||type>12)return;
    // This code runs only from constructors, sample-rate setup or HTTP controls.
    std::lock_guard<std::mutex> lock(studioSlotBuildMutex_);
    const auto& old=studioSlotDsp_[rack][slot];
    if(old&&old->type()==type)return;
    // Built here, on the control thread, then published: the audio thread never
    // allocates. The previous processor keeps ringing out on the audio side.
    auto next=std::make_shared<StudioSlotDSP>();
    next->init(static_cast<float>(hostSampleRate_),type);
    publishStudioSlotLocked(rack,slot,std::move(next));
}

void StagePianoEngine::publishStudioSlotLocked(int rack,int slot,std::shared_ptr<StudioSlotDSP> next){
    auto& owner=studioSlotDsp_[rack][slot];
    if(owner)studioRetired_.push_back({std::move(owner),audioEpoch_.now()});
    owner=std::move(next);
    studioSlotRaw_[rack][slot].store(owner.get(),std::memory_order_release);
    collectStudioRetiredLocked();
}

void StagePianoEngine::collectStudioRetiredLocked(){
    // A retired processor is freed once the audio thread has completed two callbacks
    // since it was unpublished AND it no longer rings out in a spill-over lane.
    const uint64_t now=audioEpoch_.now();
    studioRetired_.erase(std::remove_if(studioRetired_.begin(),studioRetired_.end(),
        [&](const RetiredSlot& r){
            if(now<r.epoch+2)return false;
            for(const auto& rack:studioHeld_)for(const auto& lanes:rack)for(const auto& held:lanes)
                if(held.load(std::memory_order_acquire)==r.dsp.get())return false;
            return true;
        }),studioRetired_.end());
}

bool StagePianoEngine::setStudioSlot(int rack,int slot,int field,float value){
    if(rack<0||rack>=STUDIO_RACK_COUNT||slot<0||slot>=STUDIO_SLOTS||field<0||field>STUDIO_PARAM_COUNT+2||!std::isfinite(value))return false;
    auto release=[&](){
        std::lock_guard<std::mutex> lock(studioSlotBuildMutex_);
        publishStudioSlotLocked(rack,slot,{});
    };
    if(field==0){
        const int type=std::clamp(int(std::lround(value)),0,12);
        if(type==0 || type!=studioSlotType_[rack][slot].load()){
            release();
            studioSlotType_[rack][slot].store(type);
            studioSlotVersion_[rack][slot].store(type==0?1:(type==5 || type==6?3:2));
            // Fresh effect slots start with useful musical settings. Loading a
            // v1 program restores its original version and full parameter set.
            if(type==5){
                studioSlotParam_[rack][slot][6].store(0.f); // local tempo
                studioSlotParam_[rack][slot][7].store((5.f+.01f)/16.f); // quarter note
                studioSlotParam_[rack][slot][8].store((120.f-30.f)/270.f); // independent 120 BPM
            }else if(type==6){
                // New shimmer has a visible, audible octave from the first load.
                studioSlotParam_[rack][slot][6].store(1.f); // +12 semitones legacy range
                studioSlotParam_[rack][slot][12].store(.30f); // SINGLE
                studioSlotParam_[rack][slot][16].store((7.f+24.f)/48.f); // secondary +7st
                studioSlotParam_[rack][slot][17].store(.35f);
                studioSlotParam_[rack][slot][18].store(.50f);
                studioSlotParam_[rack][slot][19].store(.50f);
            }
            if(type==12){
                const float eqDefaults[16]={.75f,0.f,1.f,.5f,.5f,.5f,.5f,.5f,.5f,.25f,0.f,0.f,.5f,.25f,.25f,.5f};
                for(int k=0;k<16;++k)studioSlotParam_[rack][slot][k].store(eqDefaults[k]);
            }else if(type==9){
                studioSlotParam_[rack][slot][5].store(0.f); // hard knee
                studioSlotParam_[rack][slot][6].store(0.f); // detector HPF off
                studioSlotParam_[rack][slot][7].store(1.f); // 100% compressed wet
                studioSlotParam_[rack][slot][8].store(0.f); // peak detector
            }else if(type==7){
                studioSlotParam_[rack][slot][1].store(0.f); // soft saturation
                studioSlotParam_[rack][slot][2].store(.75f);
                studioSlotParam_[rack][slot][3].store(.55f);
                studioSlotParam_[rack][slot][4].store(.5f);
                studioSlotParam_[rack][slot][5].store(.5f);
            }else if(type==2){
                studioSlotParam_[rack][slot][4].store(.7f); // six all-pass stages
                studioSlotParam_[rack][slot][5].store(.3f); // low sweep frequency
                studioSlotParam_[rack][slot][6].store(.5f); // high sweep frequency
                studioSlotParam_[rack][slot][7].store(.25f); // stereo offset
            }
            // v32: a slot with an effect is prepared even while OFF (memory only, no
            // CPU): switching it on during a performance is instant.
            if(type==0)studioSlotEnabled_[rack][slot].store(false);
            else prepareStudioSlot(rack,slot,type);
        }
    }else if(field==1){
        // v32: switching a slot OFF keeps its processor allocated (warm) so it comes
        // back instantly and its tail rings out; an OFF slot costs no CPU once silent.
        // Only an EMPTY slot (type 0) frees its processor.
        const bool enabled=value>=.5f&&studioSlotType_[rack][slot].load()>0;
        if(enabled)prepareStudioSlot(rack,slot,studioSlotType_[rack][slot].load());
        studioSlotEnabled_[rack][slot].store(enabled);
    }else if(field==18){
        studioSlotVersion_[rack][slot].store(std::clamp(int(std::lround(value)),1,4));
    }else studioSlotParam_[rack][slot][field<=17?field-2:field-3].store(std::clamp(value,0.f,1.f));
    return true;
}
bool StagePianoEngine::setStudioSend(int rack,int source,int slot,float amount){
    const int count=(rack==0?5:rack==1?SYNTH_LAYER_COUNT:rack==2?1:3);
    if(rack<0||rack>=4||source<0||source>=count||slot<0||slot>=5||!std::isfinite(amount))return false;
    studioSends_[rack][source][slot].store(std::clamp(amount,0.f,1.f));return true;
}
bool StagePianoEngine::setStudioEq(int rack,int band,float gain){
    if(rack<0||rack>=4||band<0||band>=5||!std::isfinite(gain))return false;
    studioEqDb_[rack][band].store(std::clamp(gain,-15.f,15.f));studioEqDirty_[rack].store(true);return true;
}
bool StagePianoEngine::setStudioEqParam(int rack,int field,float value){
    if(rack<0||rack>=STUDIO_RACK_COUNT||field<0||field>16||!std::isfinite(value))return false;
    switch(field){
      case 0: studioEqHighPass_[rack].store(std::clamp(value,20.f,1000.f));break;
      case 1: studioEqLowPass_[rack].store(std::clamp(value,1000.f,20000.f));break;
      case 2: studioEqInputDb_[rack].store(std::clamp(value,-24.f,24.f));break;
      case 3: studioEqOutputDb_[rack].store(std::clamp(value,-24.f,24.f));break;
      case 4: studioEqPosition_[rack].store(std::clamp(int(std::lround(value)),0,STUDIO_SLOTS));break;
      case 15: studioEqHighSlope_[rack].store(normalizeEqSlope(int(std::lround(value))));break;
      case 16: studioEqLowSlope_[rack].store(normalizeEqSlope(int(std::lround(value))));break;
      default: if(field<10)studioEqFreq_[rack][field-5].store(std::clamp(value,30.f,16000.f));
               else studioEqQ_[rack][field-10].store(std::clamp(value,.3f,12.f));break;
    }
    if(field!=4)studioEqDirty_[rack].store(true);
    return true;
}
bool StagePianoEngine::setStudioEqEnabled(int rack,bool enabled){
    if(rack<0||rack>=4)return false;
    studioEqEnabled_[rack].store(enabled);studioEqDirty_[rack].store(true);return true;
}
StudioRackConfig StagePianoEngine::getStudioRack(int rack) const{
    StudioRackConfig cfg;
    if(rack<0||rack>=4)return cfg;
    cfg.eqEnabled=studioEqEnabled_[rack].load();
    cfg.eqHighPass=studioEqHighPass_[rack].load();cfg.eqLowPass=studioEqLowPass_[rack].load();
    cfg.eqHighPassSlope=studioEqHighSlope_[rack].load();cfg.eqLowPassSlope=studioEqLowSlope_[rack].load();
    cfg.eqInputDb=studioEqInputDb_[rack].load();cfg.eqOutputDb=studioEqOutputDb_[rack].load();
    cfg.eqPosition=studioEqPosition_[rack].load();
    for(int i=0;i<5;++i){
        cfg.eqDb[i]=studioEqDb_[rack][i].load();
        cfg.eqFreq[i]=studioEqFreq_[rack][i].load();cfg.eqQ[i]=studioEqQ_[rack][i].load();
        auto& s=cfg.slots[i];
        s.type=studioSlotType_[rack][i].load();s.version=studioSlotVersion_[rack][i].load();s.enabled=studioSlotEnabled_[rack][i].load();
        for(int k=0;k<STUDIO_PARAM_COUNT;k++)s.p[k]=studioSlotParam_[rack][i][k].load();
        for(int j=0;j<5;j++)cfg.sends[i][j]=studioSends_[rack][i][j].load();
    }
    return cfg;
}
void StagePianoEngine::setStudioRack(int rack,const StudioRackConfig& cfg){
    if(rack<0||rack>=4)return;
    setStudioEqEnabled(rack,cfg.eqEnabled);
    for(int band=0;band<5;band++){
        setStudioEq(rack,band,cfg.eqDb[band]);
        setStudioEqParam(rack,band+5,cfg.eqFreq[band]);setStudioEqParam(rack,band+10,cfg.eqQ[band]);
    }
    setStudioEqParam(rack,0,cfg.eqHighPass);setStudioEqParam(rack,1,cfg.eqLowPass);
    setStudioEqParam(rack,15,float(cfg.eqHighPassSlope));setStudioEqParam(rack,16,float(cfg.eqLowPassSlope));
    setStudioEqParam(rack,2,cfg.eqInputDb);setStudioEqParam(rack,3,cfg.eqOutputDb);
    setStudioEqParam(rack,4,float(cfg.eqPosition));
    for(int slot=0;slot<5;slot++){
        setStudioSlot(rack,slot,0,float(cfg.slots[slot].type));
        for(int i=0;i<STUDIO_PARAM_COUNT;++i)setStudioSlot(rack,slot,i<16?i+2:i+3,cfg.slots[slot].p[i]);
        setStudioSlot(rack,slot,18,float(cfg.slots[slot].version));
        setStudioSlot(rack,slot,1,cfg.slots[slot].enabled?1.f:0.f);
    }
    for(int src=0;src<(rack==0?5:rack==1?SYNTH_LAYER_COUNT:rack==2?1:3);++src)
      for(int slot=0;slot<5;++slot)setStudioSend(rack,src,slot,cfg.sends[src][slot]);
}

bool StagePianoEngine::swapStudioSlots(int rack,int first,int second){
    if(rack<0||rack>=STUDIO_RACK_COUNT||first<0||first>=STUDIO_SLOTS||second<0||second>=STUDIO_SLOTS)return false;
    if(first==second)return true;
    auto cfg=getStudioRack(rack);
    std::swap(cfg.slots[first],cfg.slots[second]);
    for(int source=0;source<STUDIO_MAX_SOURCES;++source)
        std::swap(cfg.sends[source][first],cfg.sends[source][second]);
    setStudioRack(rack,cfg);
    return true;
}

void StagePianoEngine::setModuleFxParam(bool synth,int field,float v){
    if(field<0||field>=30 || !std::isfinite(v))return;
    const float mins[30]={0,.1f,0,0,0,40,0,0,0,0,0,.05f,0,0,0,.05f,0,
                          0,.1f,0,0,0,0,.5f,0,0,.05f,0,-.85f,0};
    const float maxs[30]={1,6,1,1,1,1200,.85f,1,1,3,1,1,1,1,1,12,1,
                          1,6,1,.85f,1,1,14,1,1,5,1,.85f,1};
    moduleFxParams_[synth?0:1][field].store(std::clamp(v,mins[field],maxs[field]));
}

ModuleFxConfig StagePianoEngine::getModuleFxSnapshot(bool synth) const{
    ModuleFxConfig c;
    const auto& a=moduleFxParams_[synth?0:1];
    c.chorusEnabled=a[0].load()>=.5f;c.chorusRate=a[1].load();c.chorusDepth=a[2].load();c.chorusMix=a[3].load();
    c.delayEnabled=a[4].load()>=.5f;c.delayMs=a[5].load();c.delayFeedback=a[6].load();c.delayMix=a[7].load();
    c.reverbEnabled=a[8].load()>=.5f;c.reverbType=int(a[9].load());c.reverbMix=a[10].load();c.reverbSize=a[11].load();
    c.driveEnabled=a[12].load()>=.5f;c.driveAmount=a[13].load();
    c.autoPanEnabled=a[14].load()>=.5f;c.autoPanRate=a[15].load();c.autoPanDepth=a[16].load();
    c.phaserEnabled=a[17].load()>=.5f;c.phaserRate=a[18].load();c.phaserDepth=a[19].load();
    c.phaserFeedback=a[20].load();c.phaserMix=a[21].load();
    c.tremoloEnabled=a[22].load()>=.5f;c.tremoloRate=a[23].load();c.tremoloDepth=a[24].load();
    c.flangerEnabled=a[25].load()>=.5f;c.flangerRate=a[26].load();c.flangerDepth=a[27].load();
    c.flangerFeedback=a[28].load();c.flangerMix=a[29].load();
    return c;
}

void StagePianoEngine::setEqBandGainDb(int bandIdx, float db) {
    if (bandIdx >= 0 && bandIdx < 5) {
        eqGainDb_[bandIdx].store(std::clamp(db, -15.0f, 15.0f));
        eqDirty_.store(true);
    }
}

void StagePianoEngine::setEqBandFreqHz(int bandIdx, float hz) {
    if (bandIdx >= 0 && bandIdx < 5) {
        eqFreqHz_[bandIdx].store(std::clamp(hz, 20.0f, 20000.0f));
        eqDirty_.store(true);
    }
}

void StagePianoEngine::setEqBandQ(int bandIdx, float q) {
    if (bandIdx >= 0 && bandIdx < 5) {
        eqQ_[bandIdx].store(std::clamp(q, 0.25f, 12.0f));
        eqDirty_.store(true);
    }
}

SynthPresetParams StagePianoEngine::getCurrentParams() const {
    SynthPresetParams p;
    p.masterBpm=masterBpm_.load();p.delayTempoSync=delayTempoSync_.load();p.delayDivision=delayDivision_.load();
    p.studioRoutingEnabled=studioRoutingEnabled_.load();
    for(int r=0;r<STUDIO_RACK_COUNT;++r)p.studioRacks[r]=getStudioRack(r);
    p.pianoEnabled = pianoEnabled_.load();
    for(int i=0;i<3;i++){p.pianoZone.low=moduleZoneLow_[0].load();p.pianoZone.high=moduleZoneHigh_[0].load();
       p.synthZone.low=moduleZoneLow_[1].load();p.synthZone.high=moduleZoneHigh_[1].load();
       p.organZone.low=moduleZoneLow_[2].load();p.organZone.high=moduleZoneHigh_[2].load();}
    for(int i=0;i<PIANO_LAYER_COUNT;i++) {p.pianoLayerZones[i].low=pianoZoneLow_[i].load();p.pianoLayerZones[i].high=pianoZoneHigh_[i].load();
        p.pianoLayerZones[i].octave=pianoLayerOct_[i].load();p.pianoLayerZones[i].transpose=pianoLayerSemi_[i].load();}
    p.synthFx=getModuleFxSnapshot(true);p.organFx=getModuleFxSnapshot(false);
    for(int i=0;i<3;i++){p.synthFxSends[i].modulation=synthFxSend_[i][0].load();p.synthFxSends[i].delay=synthFxSend_[i][1].load();p.synthFxSends[i].reverb=synthFxSend_[i][2].load();}
    p.organFxSends.modulation=organFxSend_[0].load();
    p.organFxSends.delay=organFxSend_[1].load();
    p.organFxSends.reverb=organFxSend_[2].load();
    p.flangerEnabled=flangerEnabled_.load();p.flangerRate=flangerRate_.load();
    p.flangerDepth=flangerDepth_.load();p.flangerFeedback=flangerFeedback_.load();p.flangerMix=flangerMix_.load();
    p.masterDriveEnabled=masterDriveOn_.load();p.masterDriveAmount=masterDriveAmt_.load();
    p.masterAutoPanEnabled=masterAutoPanOn_.load();p.masterAutoPanRate=masterAutoPanRate_.load();p.masterAutoPanDepth=masterAutoPanDepth_.load();
    p.pianoVolume = pianoVolume_.load();
    p.pianoPan=modulePan(0);p.synthPan=modulePan(1);p.organPan=modulePan(2);
    p.pianoTune=moduleTune(0);p.synthTune=moduleTune(1);p.organTune=moduleTune(2);
    for (int i = 0; i < PIANO_LAYER_COUNT; ++i) {
        p.pianoLayerEnabled[i] = pianoLayerEnabled_[i].load();
        p.pianoLayerVolume[i] = pianoLayerVolume_[i].load();
        auto& cfg = p.pianoLayerSample[i];
        cfg.attackSec = pianoLayerSample_[i][0].load();
        cfg.decaySec = pianoLayerSample_[i][1].load();
        cfg.sustainLevel = pianoLayerSample_[i][2].load();
        cfg.releaseSec = pianoLayerSample_[i][3].load();
        cfg.loopHold = pianoLayerSample_[i][4].load();
        cfg.pan = pianoLayerSample_[i][5].load();
        cfg.fineTuneCents = pianoLayerSample_[i][6].load();
        cfg.cutoffHz = pianoLayerSample_[i][7].load();
        cfg.resonanceQ = pianoLayerSample_[i][8].load();
        cfg.gainDb = pianoLayerSample_[i][9].load();
        cfg.wheelCutoffOctaves=pianoWheelCutoff_[i].load();
        cfg.wheelAmpDb=pianoWheelAmpDb_[i].load();
        cfg.wheelCutoffEnabled=pianoWheelCutoffEnabled_[i].load();
        cfg.wheelCutoffInvert=pianoWheelCutoffInvert_[i].load();
        cfg.wheelAmpEnabled=pianoWheelAmpEnabled_[i].load();
        cfg.wheelVibratoEnabled=pianoWheelVibratoEnabled_[i].load();
        cfg.wheelVibratoCents=pianoWheelVibratoCents_[i].load();
        cfg.wheelVibratoRateHz=pianoWheelVibratoRate_[i].load();
        cfg.monoMode=pianoMonoMode_[i].load();
        cfg.legatoEnabled=pianoLegatoEnabled_[i].load();
        cfg.portamentoEnabled=sampleGlideOn_[i].load();
        cfg.portamentoSec=sampleGlideTime_[i].load();
        cfg.portamentoMode=sampleGlideMode_[i].load();
        cfg.insertFx=pianoInsertFx_[size_t(i)];
        cfg.soundEqEnabled=soundEqEnabled_[i].load();
        cfg.soundEqHighPass=soundEqHighPass_[i].load();cfg.soundEqLowPass=soundEqLowPass_[i].load();
        cfg.soundEqHighPassSlope=soundEqHighSlope_[i].load();cfg.soundEqLowPassSlope=soundEqLowSlope_[i].load();
        for(int b=0;b<3;++b){cfg.soundEqFreq[b]=soundEqFreq_[i][b].load();
            cfg.soundEqQ[b]=soundEqQ_[i][b].load();cfg.soundEqGain[b]=soundEqGain_[i][b].load();}
    }
    for (int src = 0; src < FX_SOURCE_COUNT; ++src) {
        p.fxSends[src].modulation = fxSends_[src][0].load();
        p.fxSends[src].delay = fxSends_[src][1].load();
        p.fxSends[src].reverb = fxSends_[src][2].load();
    }
    p.maxVoices = maxVoicesLimit_.load();
    p.transposeSemis = transposeSemis_.load();
    p.octaveShift = octaveShift_.load();
    p.masterTuneCents = masterTuneCents_.load();
    p.softRelease = softRelease_.load();
    p.kbdTouch = kbdTouch_.load();

    p.cutoffHz = cutoffHz_.load();
    p.resonanceQ = resonanceQ_.load();
    p.attackSec = attackSec_.load();
    p.decaySec = decaySec_.load();
    p.sustainLevel = sustainLevel_.load();
    p.releaseSec = releaseSec_.load();
    p.loopHold = loopHold_.load();
    p.volume = masterGain_.load();
    p.pan = pan_.load();

    p.eqEnabled = eqEnabled_.load();
    p.lowCutHz = lowCutHz_.load();
    p.highCutHz = highCutHz_.load();
    for (int i = 0; i < 5; ++i) {
        p.eqBands[i].freqHz = eqFreqHz_[i].load();
        p.eqBands[i].q = eqQ_[i].load();
        p.eqBands[i].gainDb = eqGainDb_[i].load();
    }

    p.chorusEnabled = chorusEnabled_.load();
    p.chorusRateHz = chorusRate_.load();
    p.chorusDepth = chorusDepth_.load();
    p.chorusMix = chorusMix_.load();
    p.chorusVoices = chorusVoices_.load();
    p.chorusWidth = chorusWidth_.load();
    p.chorusFeedback = chorusFeedback_.load();
    p.chorusTone = chorusTone_.load();
    p.chorusDelay = chorusDelay_.load();

    p.phaserEnabled = phaserEnabled_.load();
    p.phaserRateHz = phaserRate_.load();
    p.phaserDepth = phaserDepth_.load();
    p.phaserFeedback = phaserFeedback_.load();
    p.phaserMix = phaserMix_.load();

    p.tremoloEnabled = tremoloEnabled_.load();
    p.tremoloRateHz = tremoloRate_.load();
    p.tremoloDepth = tremoloDepth_.load();

    p.delayEnabled = delayEnabled_.load();
    p.delayTimeMs = delayTimeMs_.load();
    p.delayFeedback = delayFeedback_.load();
    p.delayDamping = delayDamping_.load();
    p.delayMix = delayMix_.load();
    p.delayPingPong = delayPingPong_.load();

    p.reverbEnabled = reverbEnabled_.load();
    p.reverbType = reverbType_.load();
    p.reverbMix = reverbMix_.load();
    p.reverbSize = reverbSize_.load();
    p.reverbDamping = reverbDamping_.load();
    p.reverbPreDelayMs = reverbPreDelayMs_.load();
    p.reverbShimmer = reverbShimmer_.load();

    p.shimmerShiftSt = shimmerShiftSt_.load();
    p.shimmerFeedback = shimmerFeedback_.load();
    p.shimmerDiffusion = shimmerDiffusion_.load();
    p.shimmerLowCutHz = shimmerLowCutHz_.load();
    p.shimmerHighCutHz = shimmerHighCutHz_.load();
    p.shimmerModRateHz = shimmerModRateHz_.load();
    p.shimmerModDepth = shimmerModDepth_.load();
    p.shimmerPitchMode = shimmerPitchMode_.load();
    p.shimmerReverbMode = shimmerReverbMode_.load();
    p.shimmerColorMode = shimmerColorMode_.load();

    p.synthEnabled = multiSynth_.synthEnabled;
    p.synthPresetId = multiSynth_.synthPresetId;
    p.synthMasterVol = multiSynth_.synthMasterVol;
    p.synthMacroCutoff = multiSynth_.synthMacroCutoff;
    p.synthWheelGlobalCutoff = multiSynth_.synthWheelGlobalCutoff;
    p.synthWheelGlobalAmpDb = multiSynth_.synthWheelGlobalAmpDb;
    p.synthOutGainDb = multiSynth_.synthOutGainDb;
    p.synthVoiceMaster=multiSynth_.synthVoiceMaster;
    p.synthVoiceMode = multiSynth_.synthVoiceMode;
    p.synthSeqGlobalClock = multiSynth_.synthSeqGlobalClock;
    p.synthSeqSharePattern = multiSynth_.synthSeqSharePattern;
    p.synthMacros = multiSynth_.macros;
    p.modMatrix = multiSynth_.getModMatrix();
    p.synthPortamento = multiSynth_.synthPortamento;
    p.synthPortamentoEnabled = multiSynth_.synthPortamentoEnabled;
    p.synthGlideMode = multiSynth_.synthGlideMode;
    p.synthSlide = multiSynth_.synthSlide;
    for (int i = 0; i < SYNTH_LAYER_COUNT; ++i) {
        p.synthLayers[i] = multiSynth_.layers[i].cfg;
    }
    for (int i = 0; i < SYNTH_NUM_LFOS; ++i) {
        const auto& src = multiSynth_.lfos[i];
        auto& dst = p.synthLfos[i];
        dst.enabled = src.enabled;
        dst.waveType = src.waveType;
        dst.rateHz = src.rateHz;
        dst.depth = src.depth;
        dst.delaySec = src.delaySec;
        dst.fadeSec = src.fadeSec;
        dst.destPitch = src.destPitch;
        dst.destCutoff = src.destCutoff;
        dst.destVolume = src.destVolume;
        dst.destPan = src.destPan;
        dst.destPhase = src.destPhase;
        dst.keySync = src.keySync;
    }
    for (int i = 0; i < SYNTH_NUM_ENVS; ++i) {
        const auto& src = multiSynth_.envelopes[i];
        auto& dst = p.synthEnvelopes[i];
        dst.delaySec = src.delaySec;
        dst.attackSec = src.attackSec;
        dst.holdSec = src.holdSec;
        dst.decaySec = src.decaySec;
        dst.sustainLevel = src.sustainLevel;
        dst.releaseSec = src.releaseSec;
    }
    p.synthLfoEnabled = multiSynth_.lfos[0].enabled;
    p.synthLfoWave = multiSynth_.lfos[0].waveType;
    p.synthLfoRate = multiSynth_.lfos[0].rateHz;
    p.synthLfoDepth = multiSynth_.lfos[0].depth;
    p.synthLfoPitch = multiSynth_.lfos[0].destPitch;
    p.synthLfoCutoff = multiSynth_.lfos[0].destCutoff;

    p.organEnabled = organModule_.organEnabled;
    p.organModel = organModule_.organModel;
    p.organVolume = organModule_.organVolume;
    p.organRotaryFast = organModule_.organRotaryFast;
    p.organDrive = organModule_.organDrive;
    p.organRotaryMix = organModule_.rotaryMix;
    p.organRotaryFastRate=organModule_.rotaryFastRate;
    p.organRotaryRamp=organModule_.rotaryRamp;
    p.organRotaryStereoWidth=organModule_.rotaryStereoWidth;
    p.organVibratoDepth = organModule_.vibratoDepth;
    p.organVibratoRate = organModule_.vibratoRate;
    p.organChorusEnabled = organModule_.chorusEnabled;
    p.organChorusRate = organModule_.chorusRate;
    p.organChorusDepth = organModule_.chorusDepth;
    p.organChorusMix = organModule_.chorusMix;
    p.organChorusWidth = organModule_.chorusWidth;
    p.organChorusFeedback = organModule_.chorusFeedback;
    p.organChorusTone = organModule_.chorusTone;
    p.organChorusVoices = organModule_.chorusVoices;
    p.organVibratoMode = organModule_.vibratoMode;
    p.organPercEnabled = organModule_.percussionEnabled;
    p.organPercHarmonic = organModule_.percussionHarmonic;
    p.organPercDecaySec = organModule_.percussionDecaySec;
    p.organPercLevel = organModule_.percussionLevel;
    for (int d = 0; d < 9; ++d) {
        p.drawbars[d] = organModule_.drawbars[d];
    }

    return p;
}

void StagePianoEngine::getActiveKeyStates(std::array<uint8_t, 128>& outStates) const {
    for (size_t i = 0; i < 128; ++i) {
        outStates[i] = keyStates_[i].load(std::memory_order_relaxed);
    }
}

void StagePianoEngine::pushMidiEvent(uint8_t type, int d1, int d2, int src, int ch) {
    size_t pos = midiWriteIdx_.load(std::memory_order_relaxed);
    for (;;) {
        auto& slot = midiQueue_[pos % MIDI_QUEUE_SIZE];
        const size_t seq = slot.sequence.load(std::memory_order_acquire);
        const auto delta = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
        if (delta == 0) {
            if (midiWriteIdx_.compare_exchange_weak(pos, pos + 1,
                    std::memory_order_relaxed, std::memory_order_relaxed)) {
                slot.event = {type, d1, d2, uint8_t(std::clamp(src, 0, 15)), uint8_t(std::clamp(ch, 0, 16))};
                slot.sequence.store(pos + 1, std::memory_order_release);
                return;
            }
        } else if (delta < 0) {
            midiDropped_.fetch_add(1, std::memory_order_relaxed);
            if (type == 2 && d1 >= 0 && d1 < 128)
                emergencyNoteOffMasks_[size_t(d1 / 64)].fetch_or(uint64_t(1) << (d1 % 64), std::memory_order_release);
            else if (type == 4)
                emergencyAllNotesOff_.store(true, std::memory_order_release);
            else if (type == 3 && d1 == 0)
                emergencySustainOff_.store(true, std::memory_order_release);
            return;
        } else {
            pos = midiWriteIdx_.load(std::memory_order_relaxed);
        }
    }
}

void StagePianoEngine::noteOn(int midiKey, int velocity, int source, int channel) {
    if (velocity <= 0) {
        noteOff(midiKey, source, channel);
        return;
    }
    lastMidiNote_.store(midiKey, std::memory_order_relaxed);
    lastMidiVel_.store(velocity, std::memory_order_relaxed);
    midiEventSeq_.fetch_add(1, std::memory_order_relaxed);
    pushMidiEvent(1, midiKey, velocity, source, channel);
}

void StagePianoEngine::noteOff(int midiKey, int source, int channel) {
    pushMidiEvent(2, midiKey, 0, source, channel);
}

void StagePianoEngine::setSustainPedal(bool down, int source, int channel) {
    sustainPedalState_.store(down, std::memory_order_relaxed);
    pushMidiEvent(3, down ? 1 : 0, 0, source, channel);
}

void StagePianoEngine::allNotesOff() {
    pushMidiEvent(4, 0, 0);
}

void StagePianoEngine::triggerTestTone() {
    testToneFramesRemaining_.store(static_cast<uint32_t>(hostSampleRate_ * 0.6));
}

int StagePianoEngine::touchAttenuation(int velocity) const {
    float v = static_cast<float>(std::clamp(velocity, 1, 127));
    int touch = kbdTouch_.load();
    if (touch == static_cast<int>(KbdTouch::Light)) {
        v = 127.0f * std::pow(v / 127.0f, 0.72f);
    } else if (touch == static_cast<int>(KbdTouch::Heavy)) {
        v = 127.0f * std::pow(v / 127.0f, 1.38f);
    }
    double att = -200.0 * std::log10(std::max(1.0f, v) / 127.0);
    return static_cast<int>(std::clamp(std::round(att), 0.0, 200.0));
}

const StrokeSample* StagePianoEngine::selectStrokeFor(const PianoInstrument& inst, int midiKey, int velocity) const {
    if (inst.strokes.empty()) return nullptr;
    if(inst.formatType.rfind("SF2",0)==0){
        const StrokeSample* best=nullptr;
        int bestPenalty=100000000;
        for(const auto& s:inst.strokes){
            const int keyDistance=std::max({s.lokey-midiKey,midiKey-s.hikey,0});
            const int velocityDistance=std::max({s.lovel-velocity,velocity-s.hivel,0});
            const int penalty=keyDistance*1000000+velocityDistance*1000+std::abs(midiKey-s.actualRootMidi);
            if(penalty<bestPenalty){bestPenalty=penalty;best=&s;}
            if(penalty==0)break;
        }
        return best;
    }
    int att = touchAttenuation(velocity);

    int targetZone = -1;
    int minKeyDist = 999;
    for (const auto& s : inst.strokes) {
        if (s.isReleaseStroke || s.flags == 1) continue;
        if (midiKey >= s.lokey && midiKey <= s.hikey) {
            targetZone = s.zone;
            break;
        }
        int dist = std::abs(midiKey - s.actualRootMidi);
        if (dist < minKeyDist) {
            minKeyDist = dist;
            targetZone = s.zone;
        }
    }

    const StrokeSample* pick = nullptr;
    for (const auto& s : inst.strokes) {
        if (s.zone == targetZone && !s.isReleaseStroke && s.flags != 1) {
            if (!pick) {
                pick = &s;
            } else if (s.switchPoint > 0 && s.switchPoint <= att) {
                pick = &s;
            } else if (s.switchPoint == 0 && s.layerIndex > 0 && velocity >= s.lovel && velocity <= s.hivel) {
                pick = &s;
            }
        }
    }
    if(pick)return pick;
    for(const auto& s:inst.strokes)if(!s.isReleaseStroke && s.flags!=1)return &s;
    return nullptr;
}

void StagePianoEngine::handleNoteOnInternal(
    const std::array<std::shared_ptr<const PianoInstrument>, PIANO_LAYER_COUNT>& instruments,
    int rawMidiKey, int velocity, uint16_t routeMask) {
    if(rawMidiKey<0 || rawMidiKey>127 || velocity<=0)return;
    int midiKey = std::clamp(rawMidiKey + transposeSemis_.load() + octaveShift_.load() * 12, 0, 127);

    // Siempre propagar NoteOn a Synth y Órgano aunque no haya piano cargado o el piano esté desactivado
    multiSynth_.synthKeyLow=moduleZoneLow_[1].load();multiSynth_.synthKeyHigh=moduleZoneHigh_[1].load();
    // v33 (M2): only the destinations routed for this note's source / channel.
    const unsigned synthLayers=(routeMask>>5)&0xFu;
    if(synthLayers) multiSynth_.noteOn(rawMidiKey, midiKey, velocity, synthLayers);
    if((routeMask&(1u<<9)) && rawMidiKey>=moduleZoneLow_[2].load() && rawMidiKey<=moduleZoneHigh_[2].load())
        organModule_.noteOn(rawMidiKey, midiKey, velocity);

    if (!pianoEnabled_.load() || rawMidiKey<moduleZoneLow_[0].load() || rawMidiKey>moduleZoneHigh_[0].load()) return;
    const uint32_t repeat=sampleRepeatCounter_[midiKey]++; // once per MIDI note, not per layer
    for (int layer = 0; layer < PIANO_LAYER_COUNT; ++layer) {
        if (!(routeMask & (1u << layer))) continue;                       // v33: not routed here
        if (rawMidiKey < pianoZoneLow_[layer].load() || rawMidiKey > pianoZoneHigh_[layer].load()) continue;
        if (!pianoLayerEnabled_[layer].load() || !instruments[layer] || instruments[layer]->strokes.empty()) continue;
        // v32.5: each layer adds its own OCTAVE / SEMI to the global transpose. Voices keep
        // the physical key (rawMidiKey), so note-off is unaffected by later changes.
        const int layerKey = std::clamp(midiKey + pianoLayerShift(layer), 0, 127);
        const auto& bank=instruments[layer];
        // SF2 may play multiple stacked regions for one note; preserve its
        // existing multi-region behavior. This priority stack is for hardware
        // sample/piano sounds with one selected stroke per key and layer.
        const bool monoLayer=pianoMonoMode_[layer].load(std::memory_order_relaxed) &&
            bank->formatType.rfind("SF2",0)!=0;
        if(monoLayer)monoHeldKeys_[layer].press(rawMidiKey,velocity);
        if(bank->formatType.rfind("SF2",0)==0){
            // SoundFonts may stack many regions (e.g. D-50 Fantasy has 4 PCM layers).
            // Trigger all matching key/velocity zones, avoiding an audio-thread allocation.
            int matched=0;
            for(const auto& s:bank->strokes)if(s.lokey<=layerKey && s.hikey>=layerKey &&
                s.lovel<=velocity && s.hivel>=velocity && s.frames>=4)++matched;
            if(matched){
                int played=0;const float weight=1.f/std::sqrt(float(std::min(matched,16)));
                for(const auto& s:bank->strokes)if(s.lokey<=layerKey && s.hikey>=layerKey &&
                   s.lovel<=velocity && s.hivel>=velocity && s.frames>=4){
                    // Limit one note to at most 16 regions for predictable CPU usage.
                    if(played>=16)break;
                    startPianoVoice(bank,layer,rawMidiKey,layerKey,velocity,&s,played==0,weight);
                    ++played;
                }
                if(monoLayer)monoHeldKeys_[layer].soundingKey=rawMidiKey;
                continue;
            }
        }
        // CNSP/NPno: type-1 strokes are auxiliary alternative lists, not
        // interchangeable velocity layers.  The directory alone does not
        // establish the hardware firmware's selection rule for these recordings.
        // Playing them on every second NoteOn (v24) substituted a different
        // piano timbre even at low velocity.  Until that selection mechanism
        // is verified with factory instruments, use ONLY a type-0 stroke.
        // NSMP4 alternates remain controlled by their explicit metadata.
        const StrokeSample* chosen=selectStrokeFor(*bank,layerKey,velocity);
        // loadNpnoNative() normally reports "Piano NPNO v5/v6", not
        // "PIANO NPNO (.npno)".  Recognize both that runtime category
        // and the source extension; otherwise the safety guard is bypassed.
        const bool isNpno=bank->formatType.rfind("Piano NPNO",0)==0 ||
                          bank->formatType.rfind("PIANO NPNO",0)==0 ||
                          bank->sourcePath.size()>=5 &&
                          bank->sourcePath.compare(bank->sourcePath.size()-5,5,".npno")==0;
        if(chosen && repeat>0 && !isNpno && bank->sampleUnison.randomStrokeMode &&
           !(pedalFor(layer) && bank->sampleUnison.blockRandomSustPed) &&
           bank->repeatAlternateCounts[layerKey]){
            const auto cnt=bank->repeatAlternateCounts[layerKey];
            const uint8_t altKey=bank->repeatAlternateKeys[layerKey][(repeat-1)%cnt];
            for(const auto& alt:bank->strokes){
                if(!alt.isReleaseStroke && alt.flags==0 && alt.actualRootMidi==altKey &&
                   alt.lovel<=velocity && velocity<=alt.hivel){chosen=&alt;break;}
            }
        }
        startPianoVoice(bank,layer,rawMidiKey,layerKey,velocity,chosen);
        if(monoLayer)monoHeldKeys_[layer].soundingKey=rawMidiKey;
    }
}

void StagePianoEngine::startPianoVoice(const std::shared_ptr<const PianoInstrument>& inst,
                                     int layer, int rawMidiKey, int midiKey, int velocity,
                                     const StrokeSample* selectedStroke, bool replaceSameKey, float polyGain, bool releaseVoice) {
    const StrokeSample* stroke = selectedStroke ? selectedStroke : selectStrokeFor(*inst, midiKey, velocity);
    if (!stroke || stroke->frames < 4) return;

    // True sample-phase legato: for MONO + LEGATO, reuse the sounding
    // oscillator when both keys use the same non-SF2 sample. This changes
    // pitch without restarting PCM, Hermite, ADSR or the sustain loop.
    // Different zones crossfade below; SF2 stacked regions keep their normal
    // multi-region triggering semantics.
    if(!releaseVoice && pianoMonoMode_[layer].load() &&
       pianoLegatoEnabled_[layer].load() &&
       inst->formatType.rfind("SF2",0)!=0){
        for(auto& previous:voices_)if(previous.active && previous.keyHeld &&
            previous.pianoLayer==layer && previous.midiKey!=rawMidiKey &&
            previous.sample==stroke && previous.sampleBank.get()==inst.get() &&
            previous.stage!=EnvStage::FastStealFade){
            const double cents=100.0*(midiKey-stroke->actualRootMidi)+
                stroke->stretchCents+stroke->fineTuneCents+masterTuneCents_.load();
            const double ratio=std::exp2(cents/1200.0);
            const double srcRate=stroke->sampleRate?stroke->sampleRate:NSP_SAMPLE_RATE;
            if(!sampleGlideOn_[layer].load() || sampleGlideTime_[layer].load()<.001f)
                previous.pitchCurrent=ratio;
            previous.pitchTarget=ratio;
            previous.step=ratio*srcRate/hostSampleRate_;
            previous.midiKey=rawMidiKey;
            previous.playedMidiKey=midiKey;
            previous.velocity=velocity;
            // A phase-continuous mono/legato note still has a NEW velocity.
            // Previously only the pitch and key changed, leaving the gain
            // attenuated by previous fast retriggers until a fresh voice fired.
            const int newAtt=touchAttenuation(velocity);
            const int newNetAtt=std::max(0,newAtt-(stroke->velocityFrom/2));
            const float newDb=-float(newNetAtt)*.18f;
            const float newGain=std::pow(10.f,newDb/20.f)*stroke->levelGainLinear*.45f*polyGain;
            previous.targetVelocityGain=newGain;
            previous.ageOrder=++voiceCounter_;
            previous.sustainedByPedal=false;
            lastSampleMidi_[layer]=midiKey;
            return;
        }
    }

    for (size_t idx = 0; idx < inst->strokes.size(); ++idx) {
        if (&inst->strokes[idx] == stroke) {
            lastTriggeredStrokeIdx_.store(static_cast<int>(idx), std::memory_order_relaxed);
            break;
        }
    }

    const size_t maxAllowed = static_cast<size_t>(std::clamp(pianoVoiceBudget_, 1, static_cast<int>(MAX_VOICES)));

    Voice* oldestSameKey = nullptr;
    int sameKeyCount = 0;
    if(replaceSameKey)for (size_t i = 0; i < maxAllowed; ++i) {
        auto& v = voices_[i];
        if (v.active && v.midiKey == rawMidiKey && v.pianoLayer == layer && v.stage != EnvStage::FastStealFade) {
            sameKeyCount++;
            if (!pedalFor(layer)) {
                v.keyHeld = false;
                v.sustainedByPedal = false;
                v.fastStealFadeStep = std::max(v.adsrLevel,0.0001f)/std::max(1.f,0.018f*float(hostSampleRate_));
                v.stage = EnvStage::FastStealFade;
            } else if(!pianoMonoMode_[layer].load()) {
                // Polyphonic pedal retrigger: gently duck previous strikes.
                // In mono mode, this compounds with legato reuse and causes
                // a perceived volume collapse on rapid repeated notes.
                v.targetVelocityGain *= 0.82f;
                if (!oldestSameKey || v.ageOrder < oldestSameKey->ageOrder) {
                    oldestSameKey = &v;
                }
            }
        }
    }
    if (pedalFor(layer) && sameKeyCount >= 4 && oldestSameKey) {
        oldestSameKey->keyHeld = false;
        oldestSameKey->sustainedByPedal = false;
        oldestSameKey->fastStealFadeStep = std::max(oldestSameKey->adsrLevel,0.0001f)/std::max(1.f,0.018f*float(hostSampleRate_));
        oldestSameKey->stage = EnvStage::FastStealFade;
    }

    Voice* target = nullptr;
    for (size_t i = 0; i < maxAllowed; ++i) {
        if (!voices_[i].active) {
            target = &voices_[i];
            break;
        }
    }

    if (!target) {
        float minScore = 1e9f;
        for (size_t i = 0; i < maxAllowed; ++i) {
            auto& v = voices_[i];
            float currentAmp = v.adsrLevel * v.sampleEnv * v.velocityGain;
            float penalty = 0.0f;
            if (v.stage == EnvStage::FastStealFade || v.stage == EnvStage::Release) penalty = 0.0f;
            else if (v.sustainedByPedal && !v.keyHeld) penalty = 0.12f;
            else penalty = 1.0f;

            float score = currentAmp + penalty;
            if (score < minScore) {
                minScore = score;
                target = &v;
            }
        }
        if (!target) target = &voices_[0];
    }

    double cents = 100.0 * (midiKey - stroke->actualRootMidi) + stroke->stretchCents + stroke->fineTuneCents + masterTuneCents_.load();
    double pitchRatio = std::pow(2.0, cents / 1200.0);

    int att = touchAttenuation(velocity);
    int netAtt = std::max(0, att - (stroke->velocityFrom / 2));
    float gainDb = -static_cast<float>(netAtt) * 0.18f;
    float velGain = std::pow(10.0f, gainDb / 20.0f) * stroke->levelGainLinear * 0.45f * polyGain;

    // SF2 sample loops sustain until note-off; the hardware's acoustic-piano loop decay
    // must never silence pads and organs rendered from SoundFonts.
    const bool sf2Voice=inst->formatType.rfind("SF2",0)==0;
    // Preserve the factory Q23 envelope, including exact hold (=1).
    const float safeDecayCoef=sf2Voice?1.f:std::clamp(stroke->decayCoef,0.f,1.f);
    const double envelopeRate=inst->formatType.find("NSMP SAMPLE")!=std::string::npos
        ? NSMP_ENVELOPE_RATE : NSP_ENVELOPE_RATE;
    const float baseLoopDecay=safeDecayCoef<=0.f?1.f:
        std::pow(safeDecayCoef,float(envelopeRate/hostSampleRate_));
    double srcRate = (stroke->sampleRate > 0) ? static_cast<double>(stroke->sampleRate) : NSP_SAMPLE_RATE;

    // If the retirement queue is saturated, drop this note-on rather than
    // allowing a shared_ptr replacement to destroy an old PCM bank in render().
    // The control worker drains the fixed queue periodically.
    if(target->sampleBank){
        const size_t write=retiredBankWrite_.load(std::memory_order_relaxed);
        if((write+1)%RETIRED_BANK_QUEUE_SIZE==
                retiredBankRead_.load(std::memory_order_acquire))return;
    }

    if(target->active && target->sample){
        // Continue rendering the stolen oscillator for 18 ms when there is a
        // spare physical voice, rather than freezing the last PCM sample.
        Voice* fading=nullptr;
        for(size_t i=maxAllowed;i<MAX_VOICES;++i){
            if(!voices_[i].active && !voices_[i].sampleBank){fading=&voices_[i];break;}
        }
        if(fading){
            retireBankFromAudio(fading->sampleBank);
            *fading=std::move(*target);
            fading->keyHeld=false;
            fading->sustainedByPedal=false;
            fading->fastStealFadeStep=fading->adsrLevel/std::max(1.f,0.018f*float(hostSampleRate_));
            fading->stage=EnvStage::FastStealFade;
            target->active=false;
        }else{
            // All 256 slots occupied: bounded fallback tail with no PCM owner.
            const size_t index=static_cast<size_t>(target-&voices_[0]);
            auto& tail=stealSmoothing_[index];
            tail.layer=target->pianoLayer;
            tail.left=target->lastRenderedL;
            tail.right=target->lastRenderedR;
            tail.total=std::max(1,static_cast<int>(hostSampleRate_*0.018));
            tail.remaining=tail.total;
        }
    }
    // A previously used slot may still own a finished instrument. Move that
    // reference to the control-thread retirement queue before reusing it.
    retireBankFromAudio(target->sampleBank);
    target->sampleBank = inst;
    target->bankSf2 = sf2Voice;
    target->bankNsmp = inst->formatType.find("NSMP SAMPLE")!=std::string::npos;
    target->pianoLayer = layer;
    target->active = true;
    target->midiKey = rawMidiKey;
    target->playedMidiKey = midiKey;
    target->velocity = velocity;
    target->keyHeld = !releaseVoice;
    target->sustainedByPedal = !releaseVoice && pedalFor(layer);
    target->isReleaseVoice=releaseVoice;
    target->stage = EnvStage::Attack;
    target->ageOrder = ++voiceCounter_;
    target->sample = stroke;
    target->pos = static_cast<double>(stroke->startFrameOffset);
    // In legato glide mode the NSMP SecondStart can skip the attack transient.
    // Only apply after verifying another physical key is held in this layer.
    if(!releaseVoice && stroke->alternateStartFrame > 0 &&
       pianoLegatoEnabled_[layer].load()){
        bool legato=false;
        for(const auto& other:voices_)if(&other!=target && other.active &&
            other.pianoLayer==layer && other.keyHeld){legato=true;break;}
        if(legato)target->pos=stroke->alternateStartFrame;
    }
    target->step = (pitchRatio * srcRate) / hostSampleRate_;
    target->wheelVibratoPhase = 0.0;
    target->wheelVibratoAmount = 0.f;
    const int previousMidi = lastSampleMidi_[layer];
    bool overlapping = false;
    for (const auto& other : voices_) {
        if (&other != target && other.active && other.pianoLayer == layer && other.keyHeld) {
            overlapping = true; break;
        }
    }
    const bool glide = sampleGlideOn_[layer].load() && sampleGlideTime_[layer].load() > 0.001f &&
        previousMidi >= 0 && (sampleGlideMode_[layer].load() == 0 || overlapping);
    target->pitchTarget = pitchRatio;
    // Absolute MIDI interval, not the ratio relative to a different sample
    // root. Two adjacent hardware velocity zones can each have ratio 1.0.
    target->pitchCurrent = glide ? pitchRatio * std::exp2((previousMidi-midiKey)/12.0) : pitchRatio;
    lastSampleMidi_[layer] = midiKey;
    if (!releaseVoice && pianoMonoMode_[layer].load()) {
        // Mono only suppresses other notes in this SAME sample layer; other
        // layers remain polyphonic. A short fade avoids hard phase cuts.
        for(auto& other:voices_)if(&other!=target && other.active &&
            other.pianoLayer==layer && other.stage!=EnvStage::FastStealFade){
            other.keyHeld=false;
            other.sustainedByPedal=false;
            other.fastStealFadeStep=std::max(other.adsrLevel,.0001f)/
                std::max(1.f,.018f*float(hostSampleRate_));
            other.stage=EnvStage::FastStealFade;
        }
    }
    target->velocityGain = velGain;
    target->targetVelocityGain = velGain;
    target->streamFade = 1.f;
    target->fastStealFadeStep = 0.f;
    target->sampleEnv = 1.0f;
    target->adsrLevel = 0.0f;
    target->lastRenderedL=0.f;
    target->lastRenderedR=0.f;
    target->baseLoopDecayPerSample = baseLoopDecay;
    target->inLoop = false;
    target->loopDecayActive=false;
    if(releaseVoice){
        target->targetVelocityGain*=0.40f;
        target->velocityGain=target->targetVelocityGain;
    }
}

void StagePianoEngine::handleNoteOffInternal(int rawMidiKey, uint16_t routeMask) {
    // v33 (M3): each destination checks its own pedal.
    for(int i=0;i<4;++i) if(routeMask&(1u<<(5+i))) multiSynth_.noteOff(rawMidiKey, pedalFor(5+i), 1u<<i);
    if(routeMask&(1u<<9)) organModule_.noteOff(rawMidiKey, pedalFor(9));

    // Physical-key last-note priority for each MONO Piano/Sample layer.
    // A legato voice adopts the newest key's midiKey. When that key goes up,
    // the older key can still be physically down; return to it without sending
    // an artificial NoteOn to Synth or Organ and without resetting the PCM
    // phase when both keys are in the same sample zone.
    if(rawMidiKey>=0 && rawMidiKey<128){
        for(int layer=0;layer<PIANO_LAYER_COUNT;++layer){
            if(!(routeMask & (1u << layer)))continue;
            if(!pianoMonoMode_[layer].load(std::memory_order_relaxed))continue;
            auto& held=monoHeldKeys_[layer];
            const bool wasDown=held.down[size_t(rawMidiKey)]!=0;
            held.release(rawMidiKey);
            if(!wasDown || held.soundingKey!=rawMidiKey)continue;
            const int resumeKey=held.mostRecent();
            if(resumeKey<0){held.soundingKey=-1;continue;}
            const auto& bank=audioInstruments_[layer];
            if(!bank || bank->strokes.empty() || !pianoLayerEnabled_[layer].load()){
                held.soundingKey=-1;
                continue;
            }
            const int playedKey=std::clamp(resumeKey+transposeSemis_.load()+
                octaveShift_.load()*12+pianoLayerShift(layer),0,127);
            const int vel=held.velocity[size_t(resumeKey)];
            const StrokeSample* stroke=selectStrokeFor(*bank,playedKey,vel);
            if(stroke){
                startPianoVoice(bank,layer,resumeKey,playedKey,vel,stroke);
                held.soundingKey=resumeKey;
            }else held.soundingKey=-1;
        }
    }

    // Key-off changes only the envelope of the sounding note.  v24 launched
    // flags=2 NPno records as separate pitched voices on *every* NoteOff,
    // including when the damper pedal was down.  Depending on the recording,
    // this produced an audible second attack after a long-held chord.
    // The reference decoder's piano audition likewise leaves release recordings
    // out of automatic playback.  Preserve the decoded auxiliary data, but
    // do not trigger it without a verified hardware release/noise routing model.
    for (auto& v : voices_) {
        if (v.active && v.midiKey == rawMidiKey && v.keyHeld && !v.isReleaseVoice &&
            v.pianoLayer >= 0 && v.pianoLayer < PIANO_LAYER_COUNT && (routeMask & (1u << v.pianoLayer))) {
            v.keyHeld = false;
            if (pedalFor(v.pianoLayer)) {
                v.sustainedByPedal = true;
            } else if (v.stage != EnvStage::FastStealFade) {
                v.sustainedByPedal = false;
                v.stage = EnvStage::Release;
            }
        }
    }
}

void StagePianoEngine::handleSustainInternal(bool down, uint16_t routeMask) {
    // v33 (M3): the pedal acts on the destinations of its source (all of them without routing).
    const uint16_t before = pedalMask_;
    pedalMask_ = uint16_t(down ? (before | routeMask) : (before & ~routeMask)) & 0x3FF;
    const uint16_t changed = uint16_t(before ^ pedalMask_);
    sustainPedalDown_ = pedalMask_ != 0;
    if (!changed) return;

    for (int i = 0; i < 4; ++i) if (changed & (1u << (5 + i))) multiSynth_.layers[size_t(i)].setSustain(down);
    if (changed & (1u << 9)) organModule_.setSustain(down);
    auto inChanged = [changed](int layer) { return layer >= 0 && layer < PIANO_LAYER_COUNT && (changed & (1u << layer)); };

    if (down) {
        for (auto& v : voices_) {
            if (v.active && v.keyHeld && inChanged(v.pianoLayer)) {
                v.sustainedByPedal = true;
            }
        }
    } else {
        for (auto& v : voices_) {
            if (v.active && !v.keyHeld && v.sustainedByPedal && inChanged(v.pianoLayer)) {
                v.sustainedByPedal = false;
                if (v.stage != EnvStage::FastStealFade) {
                    v.stage = EnvStage::Release;
                }
            }
        }
    }
}

void StagePianoEngine::render(float* outInterleavedStereo, unsigned int numFrames) {
    // Keep every audio callback within the preallocated 8192-frame stereo stems.
    // Large host buffers are split without heap allocation on the audio thread.
    if(numFrames>8192){
        for(unsigned int offset=0;offset<numFrames;offset+=8192)
            render(outInterleavedStereo+size_t(offset)*2,std::min(8192u,numFrames-offset));
        return;
    }
    std::fill(outInterleavedStereo, outInterleavedStereo + numFrames * 2, 0.0f);
    AudioEpoch::Scope epochScope{audioEpoch_};

    // Consume published bank changes without locking or allocating memory.
    // The API thread retains its own shadow pointers for UI/program state.
    size_t bankRead=bankChangeRead_.load(std::memory_order_relaxed);
    const size_t bankWrite=bankChangeWrite_.load(std::memory_order_acquire);
    while(bankRead!=bankWrite){
        auto& change=bankChanges_[bankRead];
        auto& old=audioInstruments_[change.layer];
        if(old){
            const size_t write=retiredBankWrite_.load(std::memory_order_relaxed);
            if((write+1)%RETIRED_BANK_QUEUE_SIZE==
                  retiredBankRead_.load(std::memory_order_acquire))break;
            retireBankFromAudio(old);
        }
        old=std::move(change.bank);
        lastSampleMidi_[change.layer]=-1; // no glide from a DIFFERENT Sound
        monoHeldKeys_[change.layer].clear(); // old physical notes must not resurrect the new Sound
        sampleRepeatCounter_.fill(0);
        bankRead=(bankRead+1)%BANK_CHANGE_QUEUE_SIZE;
        bankChangeRead_.store(bankRead,std::memory_order_release);
    }
    const auto& pianoInstruments=audioInstruments_;
    const float sr = static_cast<float>(hostSampleRate_);
    const float currentBpm=masterBpm_.load();
    multiSynth_.masterBpm=currentBpm;
    // v32 shared voice pool. MAX VOICES is one pool for the three modules: each may
    // use what the others are NOT using right now (voices counted in the previous
    // callback), with a guaranteed floor for every module that is switched on.
    // Previously the pool was split by weights even while a module was silent
    // (piano got 48 of 128 voices with organ + synth on), forcing constant voice
    // stealing with 3+ piano layers. A smaller budget only limits NEW notes:
    // sounding voices are never cut or faded because another module started.
    const int ceiling=std::clamp(maxVoicesLimit_.load(),16,int(MAX_VOICES));
    const bool pOn=pianoEnabled_.load() && pianoVolume_.load()>.001f;
    const bool sOn=multiSynth_.synthEnabled && multiSynth_.synthMasterVol>.001f;
    const bool oOn=organModule_.organEnabled && organModule_.organVolume>.001f;
    int pianoLayersOn=0;
    for(int layer=0;layer<PIANO_LAYER_COUNT;++layer)if(pianoLayerEnabled_[layer].load())++pianoLayersOn;
    int synthLayersOn=0;
    for(const auto& layer:multiSynth_.layers)if(layer.cfg.enabled)++synthLayersOn;
    const int usedPiano=lastPianoVoices_, usedSynth=multiSynth_.activeVoices(), usedOrgan=organModule_.activeVoices();
    const int floorPiano=pOn?std::min(ceiling,12*std::max(1,pianoLayersOn)):1;
    const int floorOrgan=oOn?std::min(ceiling,16):1;
    const int floorSynth=sOn?std::min(ceiling,8*std::max(1,synthLayersOn)):1;
    pianoVoiceBudget_=std::clamp(std::max(floorPiano,ceiling-usedSynth-usedOrgan),1,int(MAX_VOICES));
    organModule_.voiceLimit=std::clamp(std::max(floorOrgan,ceiling-usedPiano-usedSynth),1,ORGAN_MAX_VOICES);
    const int synthPool=std::max(floorSynth,ceiling-usedPiano-usedOrgan);
    multiSynth_.voiceLimitPerLayer=std::clamp(synthPool/std::max(1,synthLayersOn),1,SYNTH_MAX_VOICES_PER_LAYER);
    for(auto& layer:multiSynth_.layers)layer.voiceLimit=multiSynth_.voiceLimitPerLayer;

    for(int layer=0;layer<PIANO_LAYER_COUNT;++layer)
        if(!pianoMonoMode_[layer].load(std::memory_order_relaxed))monoHeldKeys_[layer].clear();
    size_t r = midiReadIdx_.load(std::memory_order_relaxed);
    auto forceAllNotesOff = [&]() {
        // Panic / INIT / CC 120-123: every voice fades out (18 ms piano, 12 ms synth,
        // 7 ms organ) instead of being cut mid-waveform. Effect tails ring out.
        for (auto& held:monoHeldKeys_) held.clear();
        heldRouteMask_.fill(0);                       // v33: no note is held any more
        lastSampleMidi_.fill(-1);
        for (auto& v : voices_) {
            if (!v.active || v.stage == EnvStage::FastStealFade) continue;
            v.keyHeld = false;
            v.sustainedByPedal = false;
            v.fastStealFadeStep = std::max(v.adsrLevel,0.0001f)/std::max(1.f,0.018f*float(hostSampleRate_));
            v.stage = EnvStage::FastStealFade;
        }
        multiSynth_.allNotesOff();
        organModule_.allNotesOff();
    };
    // Bounded drain prevents producer floods from monopolizing an audio callback.
    for (size_t processed = 0; processed < MIDI_QUEUE_SIZE; ++processed) {
        auto& slot = midiQueue_[r % MIDI_QUEUE_SIZE];
        const size_t seq = slot.sequence.load(std::memory_order_acquire);
        if (seq != r + 1) break; // not yet published by the producer
        const QueuedMidiEvent ev = slot.event;
        slot.sequence.store(r + MIDI_QUEUE_SIZE, std::memory_order_release);
        ++r;
        if (ev.type == 1 || ev.type == 2) {
            // v33 (M2): source 0 without a channel (on-screen keyboard, OSC, tests) plays
            // everything; MIDI inputs and the DAW follow the routing table. The note-off
            // uses the destinations its note-on went to.
            const int src = std::clamp<int>(ev.src, 0, kRouteSources - 1);
            const int key = std::clamp(ev.data1, 0, 127);
            const int ch = std::clamp<int>(ev.ch, 0, kRouteChannels - 1);
            uint16_t& held = heldRouteMask_[size_t((src * kRouteChannels + ch) * 128 + key)];
            if (ev.type == 1) {
                const uint16_t route = (src == 0 && ev.ch == 0) ? kRouteAll : midiRoute(src, ev.ch);
                held = uint16_t(held | route);        // same key twice from one source: union
                if (route) handleNoteOnInternal(pianoInstruments, ev.data1, ev.data2, route);
            } else {
                const uint16_t route = held ? held : ((src == 0 && ev.ch == 0) ? kRouteAll : midiRoute(src, ev.ch));
                held = 0;
                if (route) handleNoteOffInternal(ev.data1, route);
            }
        }
        else if (ev.type == 3) {
            const int src = std::clamp<int>(ev.src, 0, kRouteSources - 1);
            const bool all = (src == 0 && ev.ch == 0) || !sustainFollowsRouting_.load(std::memory_order_relaxed);
            handleSustainInternal(ev.data1 != 0, all ? kRouteAll : midiRoute(src, ev.ch));
        }
        else if (ev.type == 4) forceAllNotesOff();
        else if (ev.type == 5) multiSynth_.allNotesOff();
    }
    midiReadIdx_.store(r, std::memory_order_release);
    // Key releases cannot be silently lost if the queue reaches capacity.
    if (emergencyAllNotesOff_.exchange(false, std::memory_order_acquire)) forceAllNotesOff();
    if (emergencySustainOff_.exchange(false, std::memory_order_acquire)) handleSustainInternal(false);
    // In the normal case these two atomics are zero: no 128-key scan in render().
    for (int part = 0; part < 2; ++part) {
        uint64_t mask = emergencyNoteOffMasks_[size_t(part)].exchange(0, std::memory_order_acquire);
        for (int bit = 0; mask; ++bit, mask >>= 1) {
            if (mask & 1u) handleNoteOffInternal(part * 64 + bit);
        }
    }

    if (filterDirty_.exchange(false)) {
        svfFilter_.setParams(sr, cutoffHz_.load(), resonanceQ_.load());
    }
    // Studio Rack EQ coefficients are updated on their own dirty flags.
    eqDirty_.store(false,std::memory_order_relaxed);

    // Shimmer is now owned by each active StudioSlotDSP instance.
    // Updating a legacy engine here would waste CPU and allocate unused state.
    shimmerDirty_.store(false,std::memory_order_relaxed);

    const bool softRel = softRelease_.load();
    // Render fading voices outside the new budget; startPianoVoice() still
    // allocates only within the budget so voice count cannot grow past it.
    const size_t maxAllowed = MAX_VOICES;

    int activeCount = 0;
    uint8_t localKeyMap[128] = {0};

    // Only clear and mix source stems for modules currently producing sound.
    // Source buffers stay allocated across callbacks; disabled modules bypass work.
    const bool pianoEnabled=pianoEnabled_.load(std::memory_order_relaxed);
    bool fadingPiano=false;
    for(float gain:pianoLayerGainSmooth_)if(gain>0.0001f){fadingPiano=true;break;}
    const bool pianoSignal=pianoEnabled||fadingPiano;
    // A module switched OFF keeps rendering until its fade-out is silent, then costs nothing.
    const bool synthSignal = multiSynth_.needsRender();
    const bool organSignal = organModule_.needsRender();
    for (size_t source = 0; source < sourceAudio_.size(); ++source) {
        const bool enabled = source < PIANO_LAYER_COUNT ? pianoSignal :
            source < PIANO_LAYER_COUNT + SYNTH_LAYER_COUNT ? synthSignal : organSignal;
        if (!enabled) continue;
        auto& stem = sourceAudio_[source];
        // No resize() in the real-time callback; setSampleRate() preallocates this.
        std::fill_n(stem.data(), numFrames * 2, 0.0f);
    }
    const std::array<float*, SYNTH_LAYER_COUNT> synthStems = {
        sourceAudio_[PIANO_LAYER_COUNT + 0].data(),
        sourceAudio_[PIANO_LAYER_COUNT + 1].data(),
        sourceAudio_[PIANO_LAYER_COUNT + 2].data(),
        sourceAudio_[PIANO_LAYER_COUNT + 3].data()
    };
    const float midiBend = pitchBend_.load(std::memory_order_relaxed);
    const float midiMod = modWheel_.load(std::memory_order_relaxed);
    for (auto& synthLayer : multiSynth_.layers) {
        synthLayer.performancePitchBendSemis = midiBend * 2.f + moduleTune(1)*.01f;
        synthLayer.performanceModWheel = midiMod;
        multiSynth_.modBend = midiBend;
        multiSynth_.modAftertouch = aftertouch_.load(std::memory_order_relaxed);
        synthLayer.performanceWheelGlobalCutoff = multiSynth_.synthWheelGlobalCutoff;
        synthLayer.performanceWheelGlobalAmpDb = multiSynth_.synthWheelGlobalAmpDb;
    }
    organModule_.pitchBendSemis = midiBend * 2.f + moduleTune(2)*.01f;
    organModule_.modWheel = midiMod;
    multiSynth_.render(outInterleavedStereo, numFrames, &synthStems);
    organModule_.render(sourceAudio_[FX_SOURCE_COUNT - 1].data(), numFrames);

    // Contabilizar voces y teclas activas en Synth y Órgano para el visualizador de 88 teclas
    if (multiSynth_.synthEnabled) {
        for (const auto& layer : multiSynth_.layers) {
            if (!layer.cfg.enabled) continue;
            for (const auto& sv : layer.voices) {
                if (!sv.active) continue;
                activeCount++;
                if (sv.rawKey >= 0 && sv.rawKey < 128) {
                    if (sv.keyHeld) localKeyMap[sv.rawKey] = 1;
                    else if (sv.sustained && localKeyMap[sv.rawKey] == 0) localKeyMap[sv.rawKey] = 2;
                }
            }
        }
    }
    if (organModule_.organEnabled) {
        for (const auto& ov : organModule_.organVoices) {
            if (!ov.active) continue;
            activeCount++;
            if (ov.rawKey >= 0 && ov.rawKey < 128) {
                if (ov.keyHeld) localKeyMap[ov.rawKey] = 1;
                else if (ov.sustained && localKeyMap[ov.rawKey] == 0) localKeyMap[ov.rawKey] = 2;
            }
        }
    }

    // Advance existing voices even if a layer has been muted; mute only its output.
    {
        for (size_t vIdx = 0; vIdx < maxAllowed; ++vIdx) {
            auto& v = voices_[vIdx];
            if (!v.active || !v.sample || !v.sampleBank) continue;
            const PianoInstrument& voiceInst = *v.sampleBank;
            // Layer/master faders and bypass are applied once on the summed
            // stereo stem below, with a short ramp instead of a sudden gain jump.
            const bool voiceAudible=pianoSignal;
            activeCount++;

            if (v.midiKey >= 0 && v.midiKey < 128) {
                if (v.keyHeld) localKeyMap[v.midiKey] = 1;
                else if (v.sustainedByPedal && localKeyMap[v.midiKey] == 0 && (v.adsrLevel * v.sampleEnv) > 0.02f) {
                    localKeyMap[v.midiKey] = 2;
                }
            }

            const StrokeSample& s = *v.sample;
            // Validate decoded PCM and loop boundaries before indexing. A
            // malformed region must never crash the real-time callback.
            const uint32_t available=s.readyFrames ?
                s.readyFrames->load(std::memory_order_acquire) : s.frames;
            const auto usable=std::min({size_t(s.frames),s.pcmL.size(),s.pcmR.size()});
            if(usable<4){v.active=false;v.stage=EnvStage::Idle;continue;}
            // Reads never cross the published prefix, even during parallel decoding.
            const uint32_t safeAvail=std::min<uint32_t>(available,static_cast<uint32_t>(usable));
            uint32_t loopStart=s.loop.start;
            uint32_t loopLen=s.loop.length;
            const uint64_t autoLoop=s.publishedAutoLoop ?
                s.publishedAutoLoop->load(std::memory_order_acquire):0;
            const bool runtimeAutoLoop=!s.loop.hasLoop && autoLoop!=0;
            if(runtimeAutoLoop){
                loopStart=static_cast<uint32_t>(autoLoop>>32);
                const uint32_t end=static_cast<uint32_t>(autoLoop);
                loopLen=end>loopStart?end-loopStart:0;
            }
            const bool loopValid=!v.isReleaseVoice && !s.explicitOneShot &&
                (s.loop.hasLoop||runtimeAutoLoop) && loopLen>=4 &&
                loopStart<safeAvail && loopLen<=safeAvail-loopStart;
            const bool useLoop=loopValid &&
                (s.sf2LoopMode!=3 || v.keyHeld || v.sustainedByPedal);
            const double loopEndD=useLoop?double(loopStart)+loopLen:1e18;
            // Factory loops have their own authored seam. No arbitrary crossfade
            // or skipped head frames; preserve the original [start,end) exactly.
            const uint32_t loopPlayLen=loopLen;
            // Fade samples ending at nonzero amplitude before their boundary.
            const float endFadeFrames=std::max(4.f,std::min(float(usable)/4.f,
                float(s.sampleRate?s.sampleRate:44100)*0.003f));

            float damperSec = softRel ? s.softDamperT60Seconds : s.damperT60Seconds;
            if (v.playedMidiKey >= voiceInst.undampedFromKey) {
                damperSec = std::max(damperSec, 0.85f);
            }
            const int li = std::clamp(v.pianoLayer, 0, PIANO_LAYER_COUNT-1);
            const float layerAtk = std::max(0.001f, pianoLayerSample_[li][0].load());
            const float layerDec = std::max(0.02f, pianoLayerSample_[li][1].load());
            const float layerSus = std::clamp(pianoLayerSample_[li][2].load(), 0.f, 1.f);
            const float layerRel = std::max(0.02f, pianoLayerSample_[li][3].load());
            const float layerHold = std::clamp(pianoLayerSample_[li][4].load(), 0.f, 1.f);
            const float layerPan = std::clamp(pianoLayerSample_[li][5].load(), -1.f, 1.f);
            const float layerTune = pianoLayerSample_[li][6].load();
            const float attackStep = 1.f / ((v.isReleaseVoice?.003f:layerAtk) * sr);
            const float decayCoef = std::exp(-4.6f / (layerDec * sr));
            const float sustainLvl = layerSus;
            const float loopDecayExponent = (layerHold >= 0.99f) ? 0.f : std::pow((1.f-layerHold)*1.6f,1.5f);
            const float panGainL = std::sqrt(1.f-std::max(0.f,layerPan));
            const float panGainR = std::sqrt(1.f+std::min(0.f,layerPan));
            // v32.2: pitch bend is interpolated across the block (it moved in 2.9-5.8 ms steps).
            const double pitchStartMul = std::pow(2.0, (bendBlockStart_*200.f+layerTune+moduleTune(0))/1200.0);
            const double pitchEndMul = std::pow(2.0, (midiBend*200.f+layerTune+moduleTune(0))/1200.0);
            const double pitchMulStep = (pitchEndMul - pitchStartMul) / double(std::max(1u, numFrames));
            const double glideSec = sampleGlideOn_[li].load() ? sampleGlideTime_[li].load() : 0.0;
            const double glideCoeff = glideSec > .001 ? 1.0 - std::exp(-1.0 / (glideSec * sr)) : 1.0;
            const float requestedVibrato=pianoWheelVibratoEnabled_[li].load()
                ? midiMod*pianoWheelVibratoCents_[li].load():0.f;
            const float vibratoRate=pianoWheelVibratoRate_[li].load();
            const float vibratoStep=1.f-std::exp(-1.f/(.015f*sr));
            // Acoustic piano models use damper resonance. SF2 release must
            // follow SAMPLE CONFIG / ENV directly, otherwise the same knob
            // produces unexpected and very long tails on SoundFonts.
            const bool isSf2 = v.bankSf2;
            float effectiveRelSec = isSf2 ? layerRel : std::max(0.02f, damperSec * (layerRel / 0.35f));
            if(v.isReleaseVoice){
                // Fast onset for mechanical key noise, bounded natural tail.
                effectiveRelSec=std::clamp(float(s.frames)/float(std::max(1u,s.sampleRate)),.05f,.65f);
            }
            const float releaseCoef = std::exp(-6.907755f / (effectiveRelSec * sr));

            const bool factoryLoop=s.loop.hasLoop && !s.loop.isAutoLoop;
            const float effectiveLoopDecay=factoryLoop?v.baseLoopDecayPerSample:
                (loopDecayExponent<=0.f?1.f:std::pow(v.baseLoopDecayPerSample,loopDecayExponent));
            const bool nsmpFactory=factoryLoop && v.bankNsmp;

            auto wrapIdx = [&](int64_t idx, uint32_t primary) -> uint32_t {
                // Hermite's four taps must follow the cyclic region, both at
                // loopEnd and at loopStart after the first wrap. On the first
                // pass preserve the original attack/pre-loop samples.
                if(useLoop && (v.inLoop || primary>=loopStart) &&
                   (idx>=int64_t(loopEndD) || (v.inLoop && idx<int64_t(loopStart)))){
                    int64_t offset=(idx-int64_t(loopStart))%int64_t(loopPlayLen);
                    if(offset<0)offset+=loopPlayLen;
                    return uint32_t(int64_t(loopStart)+offset);
                }
                if(idx<0)return 0;
                return std::min(safeAvail-1,uint32_t(idx));
            };

            for (unsigned int n = 0; n < numFrames; ++n) {
                if (useLoop && v.pos >= loopEndD) {
                    v.pos=double(loopStart)+std::fmod(v.pos-loopEndD,double(loopPlayLen));
                    v.inLoop=true;
                    v.loopDecayActive=true;
                }
                if(!std::isfinite(v.pos)||v.pos<0 || v.pos>=double(usable)){
                    v.active=false;v.stage=EnvStage::Idle;break;
                }
                // Streaming starvation: pause position and fade out/in without
                // touching unwritten PCM (no locks, I/O or allocations).
                // A fully available loop is cyclic: Hermite's forward taps
                // wrap into its decoded start. Do not mistake loopEnd==frames
                // for streaming starvation and kill the voice before wrapping.
                if (!useLoop && (safeAvail<4 || v.pos+3.0>=double(safeAvail))) {
                    if(safeAvail==usable){
                        v.active=false;v.stage=EnvStage::Idle;break;
                    }
                    const float nextFade=std::max(0.f,v.streamFade-1.f/(0.005f*sr));
                    const float ratio=v.streamFade>1e-7f?nextFade/v.streamFade:0.f;
                    v.lastRenderedL*=ratio;v.lastRenderedR*=ratio;
                    if(voiceAudible){
                        sourceAudio_[v.pianoLayer][n*2]+=v.lastRenderedL;
                        sourceAudio_[v.pianoLayer][n*2+1]+=v.lastRenderedR;
                    }
                    v.streamFade=nextFade;
                    continue;
                }
                v.streamFade=std::min(1.f,v.streamFade+1.f/(0.005f*sr));
                uint32_t i=static_cast<uint32_t>(v.pos);
                if(nsmpFactory && i>=loopStart)v.loopDecayActive=true;
                if (!useLoop && i + 2 >= usable) {
                    v.active = false;
                    v.stage = EnvStage::Idle;
                    break;
                }

                switch (v.stage) {
                    case EnvStage::Attack:
                        v.adsrLevel += attackStep;
                        if (v.adsrLevel >= 1.0f) {
                            v.adsrLevel = 1.0f;
                            v.stage = v.isReleaseVoice ? EnvStage::Release : EnvStage::Decay;
                        }
                        break;
                    case EnvStage::Decay:
                        v.adsrLevel = sustainLvl + (v.adsrLevel - sustainLvl) * decayCoef;
                        if (std::abs(v.adsrLevel - sustainLvl) < 1e-4f) {
                            v.adsrLevel = sustainLvl;
                            v.stage = EnvStage::Sustain;
                        }
                        break;
                    case EnvStage::Sustain:
                        v.adsrLevel = sustainLvl;
                        break;
                    case EnvStage::Release:
                        v.adsrLevel *= releaseCoef;
                        if (v.adsrLevel < 1e-4f) {
                            v.adsrLevel = 0.0f;
                            v.active = false;
                            v.stage = EnvStage::Idle;
                        }
                        break;
                    case EnvStage::FastStealFade:
                        v.adsrLevel -= v.fastStealFadeStep;
                        if (v.adsrLevel <= 0.0f) {
                            v.adsrLevel = 0.0f;
                            v.active = false;
                            v.stage = EnvStage::Idle;
                        }
                        break;
                    default:
                        break;
                }
                if (!v.active) break;

                float t = static_cast<float>(v.pos - i);
                uint32_t km1 = wrapIdx(int64_t(i)-1,i);
                uint32_t k0  = wrapIdx(int64_t(i),i);
                uint32_t k1  = wrapIdx(int64_t(i)+1,i);
                uint32_t k2  = wrapIdx(int64_t(i)+2,i);

                float xm1L = s.pcmL[km1], x0L = s.pcmL[k0], x1L = s.pcmL[k1], x2L = s.pcmL[k2];
                float valL = x0L + 0.5f * t * (x1L - xm1L + t * (2.0f * xm1L - 5.0f * x0L + 4.0f * x1L - x2L + t * (3.0f * (x0L - x1L) + x2L - xm1L)));

                float xm1R = s.pcmR[km1], x0R = s.pcmR[k0], x1R = s.pcmR[k1], x2R = s.pcmR[k2];
                float valR = x0R + 0.5f * t * (x1R - xm1R + t * (2.0f * xm1R - 5.0f * x0R + 4.0f * x1R - x2R + t * (3.0f * (x0R - x1R) + x2R - xm1R)));

                // Avoid calling sin() hundreds of times per block when no wheel
                // modulation is applied (the common live-performance case).
                // CC1 vibrato is opt-in and independent for each Sound layer.
                // Smooth the wheel amount per voice to avoid sudden pitch steps.
                v.wheelVibratoAmount += (requestedVibrato - v.wheelVibratoAmount) * vibratoStep;
                double vibratoRatio=1.0;
                if(requestedVibrato>0.0001f || v.wheelVibratoAmount>0.0001f){
                    vibratoRatio=std::exp2((v.wheelVibratoAmount *
                        std::sin(v.wheelVibratoPhase))/1200.0);
                    v.wheelVibratoPhase+=6.283185307179586*std::clamp(vibratoRate,.1f,12.f)/sr;
                    if(v.wheelVibratoPhase>=6.283185307179586)v.wheelVibratoPhase-=6.283185307179586;
                }
                const float endGain=useLoop?1.f:std::clamp(float(double(usable-2)-v.pos)/endFadeFrames,0.f,1.f);
                v.velocityGain += (v.targetVelocityGain - v.velocityGain) * 0.0015f;
                float voiceGain=voiceAudible?v.velocityGain*v.sampleEnv*v.adsrLevel*endGain*
                    v.streamFade*s.sampleScale:0.f;
                const float voiceL=valL*voiceGain*panGainL;
                const float voiceR=valR*voiceGain*panGainR;
                sourceAudio_[v.pianoLayer][n*2]+=voiceL;
                sourceAudio_[v.pianoLayer][n*2+1]+=voiceR;
                v.lastRenderedL=voiceL;
                v.lastRenderedR=voiceR;

                // Portamento in pitch ratio, without re-triggering the sample envelope.
                v.pitchCurrent += (v.pitchTarget - v.pitchCurrent) * glideCoeff;
                const double smoothRatio = v.pitchTarget > 1.0e-8 ? v.pitchCurrent / v.pitchTarget : 1.0;
                v.pos += v.step * (pitchStartMul + pitchMulStep * double(n)) * smoothRatio * vibratoRatio;
                if (v.loopDecayActive) {
                    v.sampleEnv *= effectiveLoopDecay;
                    if (v.sampleEnv < 1e-5f) {
                        v.active = false;
                        v.stage = EnvStage::Idle;
                        break;
                    }
                }
            }
        }
    }

    // Release completed voice references from the callback without freeing
    // the underlying PCM vectors on the real-time thread.
    for(auto& voice:voices_){
        if(!voice.active && voice.sampleBank){
            retireBankFromAudio(voice.sampleBank);
            if(!voice.sampleBank)voice.sample=nullptr;
        }
    }

    // Short click-less continuation of a stolen voice's last output sample.
    // The old sample bank is not dereferenced and no memory is allocated here.
    for(auto& tail:stealSmoothing_){
        if(tail.remaining<=0)continue;
        const unsigned count=std::min<unsigned>(numFrames,unsigned(tail.remaining));
        float* stem=sourceAudio_[tail.layer].data();
        for(unsigned n=0;n<count;++n){
            const float envelope=float(tail.remaining)/float(tail.total);
            stem[n*2]+=tail.left*envelope;
            stem[n*2+1]+=tail.right*envelope;
            --tail.remaining;
        }
    }

    // Dezipper master/layer gain, bypass, and switches. Doing this on the stem
    // costs 5 ramps per sample instead of recomputing a ramp for every voice.
    const float gainStep=1.f-std::exp(-1.f/(0.006f*sr));
    for(int layer=0;layer<PIANO_LAYER_COUNT;++layer){
        const float requested=(pianoEnabled && pianoLayerEnabled_[layer].load())
            ? pianoVolume_.load()*pianoLayerVolume_[layer].load()*
              std::pow(10.f,pianoLayerSample_[layer][9].load()*0.05f):0.f;
        float gain=pianoLayerGainSmooth_[layer];
        if(pianoSignal){
            auto* samples=sourceAudio_[layer].data();
            for(unsigned n=0;n<numFrames;++n){
                gain+=(requested-gain)*gainStep;
                samples[n*2]*=gain;
                samples[n*2+1]*=gain;
            }
        }else gain=requested;
        pianoLayerGainSmooth_[layer]=std::abs(gain)<.00001f?0.f:gain;
    }

    // Per-piano-layer resonant low-pass: processing happens on isolated stems, BEFORE
    // routing to AUX sends. State persists per layer/channel across audio blocks.
    // Coefficients are constant per block to avoid zipper noise on each individual note.
    if(!pianoSignal){
        for(auto& f:pianoFilterStates_)f={};
    } else for (int layer = 0; layer < PIANO_LAYER_COUNT; ++layer) {
        const float wheelShift=std::clamp(pianoWheelCutoff_[layer].load(),-6.f,6.f);
        const bool cutoffOn=pianoWheelCutoffEnabled_[layer].load();
        const bool inverted=pianoWheelCutoffInvert_[layer].load();
        const float shift=cutoffOn?(inverted?(midiMod-1.f)*std::abs(wheelShift):midiMod*wheelShift):0.f;
        const float hz = std::clamp(pianoLayerSample_[layer][7].load()*
            std::exp2(shift), 40.f, sr * .45f);
        const float q = std::clamp(pianoLayerSample_[layer][8].load(), .5f, 12.f);
        const float wheelAmp=pianoWheelAmpEnabled_[layer].load()?pianoWheelAmpDb_[layer].load():0.f;
        const float wheelGain=std::pow(10.f,midiMod*std::clamp(wheelAmp,-24.f,12.f)/20.f);
        if (hz >= 19500.f && q <= .75f) {
            pianoFilterStates_[layer] = {};
            if(std::abs(wheelGain-1.f)>.0001f){auto* samples=sourceAudio_[layer].data();
                for(unsigned n=0;n<numFrames*2;n++)samples[n]*=wheelGain;}
            continue;
        }
        const float g = std::tan(3.14159265358979f * hz / sr);
        const float k = 1.f / q;
        const float a1 = 1.f / (1.f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;
        auto& f = pianoFilterStates_[layer];
        float* stem = sourceAudio_[layer].data();
        for (unsigned n = 0; n < numFrames; ++n) {
            const unsigned x = n * 2;
            const float v3L = stem[x] - f.ic2L;
            const float v1L = a1 * f.ic1L + a2 * v3L;
            const float v2L = f.ic2L + a2 * f.ic1L + a3 * v3L;
            f.ic1L = 2.f * v1L - f.ic1L; f.ic2L = 2.f * v2L - f.ic2L;
            const float v3R = stem[x+1] - f.ic2R;
            const float v1R = a1 * f.ic1R + a2 * v3R;
            const float v2R = f.ic2R + a2 * f.ic1R + a3 * v3R;
            f.ic1R = 2.f * v1R - f.ic1R; f.ic2R = 2.f * v2R - f.ic2R;
            stem[x] = v2L*wheelGain; stem[x+1] = v2R*wheelGain;
        }
    }

    // Sound-local three-band EQ: each Piano/Sample layer has an independent
    // stereo state, before its sends. No allocations, coefficient work only on
    // changed config (or sample-rate reinitialization).
    if(pianoSignal){
        for(int layer=0;layer<PIANO_LAYER_COUNT;++layer){
            if(soundEqDirty_[layer].exchange(false,std::memory_order_acq_rel)){
                PianoLayerSampleConfig eq{};
                eq.soundEqEnabled=soundEqEnabled_[layer].load();
                eq.soundEqHighPass=soundEqHighPass_[layer].load();eq.soundEqLowPass=soundEqLowPass_[layer].load();
                eq.soundEqHighPassSlope=soundEqHighSlope_[layer].load();
                eq.soundEqLowPassSlope=soundEqLowSlope_[layer].load();
                for(int b=0;b<3;++b){eq.soundEqFreq[b]=soundEqFreq_[layer][b].load();
                    eq.soundEqQ[b]=soundEqQ_[layer][b].load();eq.soundEqGain[b]=soundEqGain_[layer][b].load();}
                eq.soundEqEnabled=true;   // ON/OFF is the crossfade below
                soundLayerEq_[layer].update(sr,eq);
            }
            // EQ ON/OFF crossfades in 10 ms instead of switching the filters in/out.
            const float target=soundEqEnabled_[layer].load()?1.f:0.f;
            float& gate=soundEqGate_[size_t(layer)];
            if(target<=0.f && gate<=0.f)continue;
            float* stem=sourceAudio_[layer].data();
            for(unsigned n=0;n<numFrames;++n){
                gate+=(target-gate)*gainStep;
                if(target<=0.f && gate<1e-4f)gate=0.f;
                const float dryL=stem[2*n],dryR=stem[2*n+1];
                float l=dryL,r=dryR;
                soundLayerEq_[layer].processStereo(l,r);
                stem[2*n]=dryL+(l-dryL)*gate;stem[2*n+1]=dryR+(r-dryR)*gate;
            }
        }
    }

    // v31 Sound inserts, after the Sound EQ and before the sends. A chain with no
    // unit on is never entered. An active chain stops once the layer and its tail
    // have been silent for insertTailSeconds() (no buffer clearing on this thread).
    if(pianoSignal){
        for(int layer=0;layer<PIANO_LAYER_COUNT;++layer){
            const auto& fx=pianoInsertFx_[size_t(layer)];
            auto& dsp=pianoInsertDSP_[size_t(layer)];
            dsp.maintain();
            const bool fading=dsp.busy();
            if(!insertChainActive(fx) && !fading){pianoInsertTail_[size_t(layer)]=false;pianoInsertSilent_[size_t(layer)]=0;continue;}
            float* stem=sourceAudio_[size_t(layer)].data();
            float inPeak=0.f;
            for(unsigned n=0;n<numFrames*2;++n)inPeak=std::max(inPeak,std::abs(stem[n]));
            if(inPeak>1e-6f){pianoInsertTail_[size_t(layer)]=true;pianoInsertSilent_[size_t(layer)]=0;}
            else if(!pianoInsertTail_[size_t(layer)] && !fading)continue;
            float outPeak=0.f;
            const float bpm=multiSynth_.masterBpm;
            for(unsigned n=0;n<numFrames;++n){
                float l=stem[2*n],r=stem[2*n+1];
                dsp.process(l,r,fx,bpm);
                stem[2*n]=l;stem[2*n+1]=r;
                outPeak=std::max(outPeak,std::max(std::abs(l),std::abs(r)));
            }
            if(inPeak<=1e-6f && outPeak<1e-6f){
                pianoInsertSilent_[size_t(layer)]+=numFrames;
                if(pianoInsertSilent_[size_t(layer)]>=unsigned(sr*insertTailSeconds(fx))){
                    pianoInsertTail_[size_t(layer)]=false;pianoInsertSilent_[size_t(layer)]=0;
                }
            }else if(inPeak<=1e-6f)pianoInsertSilent_[size_t(layer)]=0;
        }
    }

    for (size_t k = 0; k < 128; ++k) {
        keyStates_[k].store(localKeyMap[k], std::memory_order_relaxed);
    }

    uint32_t testRem = testToneFramesRemaining_.load();
    if (testRem > 0) {
        const double phaseInc = (2.0 * 3.141592653589793 * 440.0) / hostSampleRate_;
        for (unsigned int n = 0; n < numFrames && testRem > 0; ++n, --testRem) {
            float env = std::min(1.0f, static_cast<float>(testRem) / (sr * 0.15f));
            float sig = static_cast<float>(std::sin(testTonePhase_) * 0.25) * env;
            outInterleavedStereo[n * 2]     += sig;
            outInterleavedStereo[n * 2 + 1] += sig;
            testTonePhase_ += phaseInc;
        }
        testToneFramesRemaining_.store(testRem);
    }

    activeVoicesCount_.store(activeCount);
    bendBlockStart_ = midiBend;
    {
        int pianoVoices=0;
        for(const auto& v:voices_)pianoVoices+=v.active?1:0;
        lastPianoVoices_=pianoVoices;
    }

    const float master = masterGain_.load();
    const float pan = pan_.load();
    const float panAngle = (pan + 1.0f) * (PI_F * 0.25f);
    const float gainL = std::cos(panAngle) * 1.41421356f * master;
    const float gainR = std::sin(panAngle) * 1.41421356f * master;

    const bool useSvf = (cutoffHz_.load() < 19800.0f || resonanceQ_.load() > 0.75f);

    const bool tremOn = tremoloEnabled_.load();
    const float tremRate = tremoloRate_.load();
    const float tremDepth = tremoloDepth_.load();

    const bool phaserOn = phaserEnabled_.load();
    const float phaserRate = phaserRate_.load();
    const float phaserDepth = phaserDepth_.load();
    const float phaserFb = phaserFeedback_.load();
    const float phaserMix = phaserMix_.load();

    const ModuleFxConfig synthLocalFx=getModuleFxSnapshot(true);
    const ModuleFxConfig organLocalFx=getModuleFxSnapshot(false);
    const bool chorusOn = chorusEnabled_.load();
    const float chorusRate = chorusRate_.load();
    const float chorusDepth = chorusDepth_.load();
    const float chorusMix = chorusMix_.load();
    const int chorusVoices = chorusVoices_.load();
    const float chorusWidth = chorusWidth_.load();
    const float chorusFeedback = chorusFeedback_.load();
    const float chorusTone = chorusTone_.load();
    const float chorusDelay = chorusDelay_.load();

    const bool delayOn = delayEnabled_.load();
    const float delayTime = delayTempoSync_.load()?tempoMilliseconds(currentBpm,delayDivision_.load()):delayTimeMs_.load();
    const float delayFb = delayFeedback_.load();
    const float delayDamp = delayDamping_.load();
    const float delayMix = delayMix_.load();
    const bool delayPP = delayPingPong_.load();

    const bool revOn = reverbEnabled_.load();
    const int revType = reverbType_.load();
    const float revMix = reverbMix_.load();
    const float revSize = reverbSize_.load();
    const float revDamp = reverbDamping_.load();
    const float revPre = reverbPreDelayMs_.load();
    const float revShimmer = reverbShimmer_.load();

    // Routing snapshot and DSP pointers are acquired once PER AUDIO BLOCK.
    // No allocations, locks, or HTTP accesses occur inside the sample loop.
    const bool studioMode=true; // Studio 7.3: legacy FX path removed
    // Master limiter timing: 0.3 ms attack, 50 ms hold (longer than one period of the
    // lowest notes and than chord beating, so the gain never ripples), 250 ms release.
    const float limiterAttack=1.f-std::exp(-1.f/(0.0003f*sr));
    const float limiterRelease=1.f-std::exp(-1.f/(0.250f*sr));
    const int limiterHoldSamples=int(0.050f*sr);
    constexpr float kLimiterCeiling=0.94f;                        // -0.5 dBFS target
    float minLimiterGain=1.f;
    std::array<StudioRackConfig,STUDIO_RACK_COUNT> studioCfg{};
    const float laneStep=1.f/(0.010f*sr);            // 10 ms send / bypass ramps
    const int silentLimit=int(0.30f*sr);              // a silent tail stops after 300 ms
    const float tailFadeLen=0.020f*sr;                // tails that time out fade in 20 ms
    // v32 studio slots. Per block, every slot lane points at its processor through a
    // lock-free raw pointer (no shared_ptr atomics / mutexes on the audio thread).
    //  - Slot ON: processed while it has input or an audible tail; silent = no CPU.
    //  - Slot OFF: same processor (kept warm by the control thread), input ramped to
    //    zero in 10 ms, its tail rings out (spill-over), then it costs nothing.
    //  - Effect replaced (program change, new type): the old processor moves to the
    //    spill lane and rings out next to the new one. No cut, no click.
    for(int rack=0;rack<STUDIO_RACK_COUNT;++rack){
        studioCfg[rack]=getStudioRack(rack);
        if(studioEqDirty_[rack].exchange(false))
            studioRackDsp_[rack].updateEq(sr,studioCfg[rack]);
        for(int slot=0;slot<STUDIO_SLOTS;++slot){
            const auto& sc=studioCfg[rack].slots[slot];
            StudioSlotDSP* published=studioSlotRaw_[rack][slot].load(std::memory_order_acquire);
            StudioSlotDSP* target=(published && sc.type>0 && published->type()==sc.type)?published:nullptr;
            auto& live=slotLive_[rack][slot];
            auto& spill=slotSpill_[rack][slot];
            if(target!=live.dsp){
                if(target && target==spill.dsp){
                    std::swap(live,spill);               // switched back while ringing out
                }else{
                    if(live.dsp && (live.input>0.f || live.tail>0))spill=live;
                    live=SlotLane{};
                    live.dsp=target;
                }
            }
            if(live.dsp){live.cfg=sc;live.dsp->syncParameters(sc);}
            if(spill.dsp)spill.dsp->syncParameters(spill.cfg);
            // Tell the control thread which processors must stay alive.
            studioHeld_[rack][slot][0].store(live.dsp,std::memory_order_release);
            studioHeld_[rack][slot][1].store(spill.dsp,std::memory_order_release);
        }
    }
    float maxL = 0.0f, maxR = 0.0f;
    float moduleMax[3]{}, sourceMax[FX_SOURCE_COUNT]{};   // v32.4 meters
    bool clipped = false;
    const float sendCoeff = 1.0f - std::exp(-1.0f / (0.012f * sr));

    // Expensive decibel conversions are invariant for an entire audio buffer.
    // Previously std::pow() ran for every sample and every rack (up to 8x/frame).
    std::array<float,STUDIO_RACK_COUNT> eqInputGain{},eqOutputGain{};
    for(int rack=0;rack<STUDIO_RACK_COUNT;++rack){
        eqInputGain[rack]=std::pow(10.f,studioCfg[rack].eqInputDb/20.f);
        eqOutputGain[rack]=std::pow(10.f,studioCfg[rack].eqOutputDb/20.f);
    }
    // How long an effect may keep ringing after its input stops.
    const auto tailSamples=[&](const StudioSlotConfig& unit)->int{
        const int model=unit.type==6?std::clamp(int(unit.p[5]*3.999f),0,3):0;
        // Shimmer feedback can deliberately sustain for well beyond 18 seconds,
        // scaled with the actual FEEDBACK knob.
        const float shimmerTail=8.f+115.f*std::pow(std::clamp(unit.p[7],0.f,1.f),2.3f);
        // A quarter/whole-note Delay at low BPM can extend far beyond ten seconds.
        float delayTailSec=10.f;
        if(unit.type==5){
            const int div=std::clamp(int(std::floor(unit.p[7]*16.f)),0,15);
            const bool sync=unit.p[6]>=.5f;
            const float bpm=unit.version>=3
                ? (sync?currentBpm:30.f+270.f*unit.p[8]) : currentBpm;
            const float ms=unit.version>=3
                ? tempoMilliseconds(bpm,div)
                : (sync?tempoMilliseconds(currentBpm,div):40.f+1450.f*unit.p[3]);
            const float repeats=std::clamp(6.f/(1.05f-.88f*unit.p[2]),4.f,36.f);
            delayTailSec=std::clamp(.5f+ms*.001f*(1.f+repeats),2.f,180.f);
        }
        const float tailSec=unit.type==6?(model==3?shimmerTail:model==2?13.f:model==1?9.f:5.f):
                           (unit.type==5?delayTailSec:2.f);
        return int(sr*tailSec);
    };
    // One sample through one lane. Returns false when the lane is idle (no CPU used).
    const auto runLane=[&](SlotLane& lane,bool wantInput,float sendL,float sendR,float& wetL,float& wetR)->bool{
        wetL=wetR=0.f;
        if(!lane.dsp)return false;
        lane.input=wantInput?std::min(1.f,lane.input+laneStep):std::max(0.f,lane.input-laneStep);
        const float inL=sendL*lane.input,inR=sendR*lane.input;
        const auto& unit=lane.cfg;
        // Shimmer Atmosphere v4 FREEZE holds the tank indefinitely while the slot is ON.
        const bool frozen=wantInput && unit.type==6 && unit.version>=4 &&
            unit.p[23]>=.5f && int(unit.p[5]*3.999f)==3;
        const bool excited=frozen || std::abs(inL)+std::abs(inR)>0.000002f;
        if(excited){lane.tail=std::max(lane.tail,tailSamples(unit));lane.silent=0;}
        else if(lane.tail>0)--lane.tail;
        else return false;
        lane.dsp->process(inL,inR,unit,wetL,wetR,currentBpm);
        if(!std::isfinite(wetL)||!std::isfinite(wetR)){wetL=wetR=0.f;}
        if(!excited){
            // Stop as soon as a reverb / modulation tail is truly silent (delays keep
            // their full time: an echo may still be pending after a silent gap).
            if(unit.type!=5){
                if(std::abs(wetL)+std::abs(wetR)<1e-6f){if(++lane.silent>silentLimit)lane.tail=0;}
                else lane.silent=0;
            }
            // A tail that times out while still audible fades instead of stopping dead.
            const float endFade=std::min(1.f,float(lane.tail)/tailFadeLen);
            wetL*=endFade;wetR*=endFade;
        }
        return true;
    };
    // A serial wet-return chain: a slot receives its own per-layer sends plus
    // the wet signal from previous slots. The original dry remains present once.
    const auto runStudioRack=[&](int rack,const float stems[STUDIO_MAX_SOURCES][2],int count,float& outL,float& outR){
        const auto& cfg=studioCfg[rack];
        outL=outR=0.f;
        for(int source=0;source<count;++source){outL+=stems[source][0];outR+=stems[source][1];}
        const auto processEq=[&](){
            // EQ ON/OFF crossfades in 10 ms; a bypassed EQ costs nothing.
            float& gate=rackEqGate_[rack];
            const bool on=cfg.eqEnabled;
            if(!on && gate<=0.f)return;
            gate=on?std::min(1.f,gate+laneStep):std::max(0.f,gate-laneStep);
            const float dryL=outL,dryR=outR;
            float l=dryL*eqInputGain[rack],r=dryR*eqInputGain[rack];
            studioRackDsp_[rack].eq.processStereo(l,r);
            l*=eqOutputGain[rack];r*=eqOutputGain[rack];
            outL=dryL+(l-dryL)*gate;outR=dryR+(r-dryR)*gate;
        };
        if(cfg.eqPosition==0)processEq();
        float previousWetL=0.f,previousWetR=0.f;
        for(int slot=0;slot<STUDIO_SLOTS;++slot){
            auto& live=slotLive_[rack][slot];
            auto& spill=slotSpill_[rack][slot];
            if(live.dsp||spill.dsp){
                float sendL=0.f,sendR=0.f,weight=0.f;
                for(int source=0;source<count;++source){
                    float& gain=studioSendSmooth_[rack][source][slot];
                    gain+=(cfg.sends[source][slot]-gain)*sendCoeff;
                    sendL+=stems[source][0]*gain;
                    sendR+=stems[source][1]*gain;
                    weight+=gain;
                }
                if(count>0)weight/=float(count);
                // Serial order matters: previous slot returns feed following units.
                sendL+=previousWetL*weight*.6f;
                sendR+=previousWetR*weight*.6f;
                float wetL=0.f,wetR=0.f;
                const bool ran=runLane(live,cfg.slots[slot].enabled,sendL,sendR,wetL,wetR);
                if(live.dsp){previousWetL=ran?wetL:0.f;previousWetR=ran?wetR:0.f;}
                outL+=wetL;outR+=wetR;
                if(spill.dsp){
                    float spillL=0.f,spillR=0.f;
                    if(runLane(spill,false,sendL,sendR,spillL,spillR)){outL+=spillL;outR+=spillR;}
                    else spill=SlotLane{};   // rung out: the control thread frees it
                }
            }
            if(cfg.eqPosition==slot+1)processEq();
        }
    };
    float modulePanL[3]{},modulePanR[3]{};
    for(int m=0;m<3;++m){
        const float pan=std::clamp(modulePan(m),-1.f,1.f);
        modulePanL[m]=std::sqrt(1.f-std::max(0.f,pan));
        modulePanR[m]=std::sqrt(1.f+std::min(0.f,pan));
    }
    for (unsigned int n = 0; n < numFrames; ++n) {
        float dryL = outInterleavedStereo[n * 2];  // test tone, if active
        float dryR = outInterleavedStereo[n * 2 + 1];
        float L=0.f,R=0.f;
        if(studioMode){
            // Three independent stereo module outs, then one global rack.
            float pianoStems[STUDIO_MAX_SOURCES][2]{},synthStems[STUDIO_MAX_SOURCES][2]{};
            float organStems[STUDIO_MAX_SOURCES][2]{};
            if(pianoSignal)for(int i=0;i<PIANO_LAYER_COUNT;++i){
                pianoStems[i][0]=sourceAudio_[i][n*2];pianoStems[i][1]=sourceAudio_[i][n*2+1];
                sourceMax[i]=std::max(sourceMax[i],std::max(std::abs(pianoStems[i][0]),std::abs(pianoStems[i][1])));
            }
            if(synthSignal)for(int i=0;i<SYNTH_LAYER_COUNT;++i){
                synthStems[i][0]=sourceAudio_[PIANO_LAYER_COUNT+i][n*2];
                synthStems[i][1]=sourceAudio_[PIANO_LAYER_COUNT+i][n*2+1];
                float& peak=sourceMax[PIANO_LAYER_COUNT+i];
                peak=std::max(peak,std::max(std::abs(synthStems[i][0]),std::abs(synthStems[i][1])));
            }
            if(organSignal){
                organStems[0][0]=sourceAudio_[FX_SOURCE_COUNT-1][n*2];
                organStems[0][1]=sourceAudio_[FX_SOURCE_COUNT-1][n*2+1];
                sourceMax[FX_SOURCE_COUNT-1]=std::max(sourceMax[FX_SOURCE_COUNT-1],std::max(std::abs(organStems[0][0]),std::abs(organStems[0][1])));
            }
            // Gain coefficients are precomputed once per buffer, never from atomics
            // and square roots in the audio inner loop.
            for(int i=0;i<PIANO_LAYER_COUNT;++i){pianoStems[i][0]*=modulePanL[0];pianoStems[i][1]*=modulePanR[0];}
            for(int i=0;i<SYNTH_LAYER_COUNT;++i){synthStems[i][0]*=modulePanL[1];synthStems[i][1]*=modulePanR[1];}
            organStems[0][0]*=modulePanL[2];organStems[0][1]*=modulePanR[2];
            float moduleStems[STUDIO_MAX_SOURCES][2]{};
            if(pianoSignal)runStudioRack(0,pianoStems,PIANO_LAYER_COUNT,moduleStems[0][0],moduleStems[0][1]);
            if(synthSignal)runStudioRack(1,synthStems,SYNTH_LAYER_COUNT,moduleStems[1][0],moduleStems[1][1]);
            if(organSignal)runStudioRack(2,organStems,1,moduleStems[2][0],moduleStems[2][1]);
            for(int m=0;m<3;++m)moduleMax[m]=std::max(moduleMax[m],std::max(std::abs(moduleStems[m][0]),std::abs(moduleStems[m][1])));
            runStudioRack(3,moduleStems,3,L,R);
            L+=dryL;R+=dryR;
        } // Studio Rack is the only effect routing path as of 7.3.

        // Master volume / pan move with a 6 ms ramp (CC 7 and the UI fader no longer zipper).
        if (masterGainSmoothL_ < 0.f) { masterGainSmoothL_ = gainL; masterGainSmoothR_ = gainR; }
        masterGainSmoothL_ += (gainL - masterGainSmoothL_) * gainStep;
        masterGainSmoothR_ += (gainR - masterGainSmoothR_) * gainStep;
        L *= masterGainSmoothL_;
        R *= masterGainSmoothR_;
        if (!std::isfinite(L)) L = 0.f;
        if (!std::isfinite(R)) R = 0.f;

        if (std::abs(L) > 0.99f || std::abs(R) > 0.99f) clipped = true;

        // v32.1 master limiter: zero latency, stereo-linked. The gain target is HELD for
        // 50 ms and the applied gain moves with a 0.3 ms attack: no gain steps (clicks)
        // and no per-cycle gain ripple (distortion). The few samples that overshoot
        // during the attack are caught by a soft knee between 0.97 and 1.0.
        const float peakNow = std::max(std::abs(L), std::abs(R));
        const float need = peakNow > kLimiterCeiling ? kLimiterCeiling / peakNow : 1.f;
        if (need <= limiterHold_) { limiterHold_ = need; limiterHoldCount_ = limiterHoldSamples; }
        else if (limiterHoldCount_ > 0) --limiterHoldCount_;
        else limiterHold_ = std::min(need, limiterHold_ + (1.f - limiterHold_) * limiterRelease);
        if (limiterHold_ < limiterGain_) limiterGain_ += (limiterHold_ - limiterGain_) * limiterAttack;
        else limiterGain_ = limiterHold_;
        minLimiterGain = std::min(minLimiterGain, limiterGain_);
        const auto safety = [](float x) {
            const float a = std::abs(x);
            return a <= 0.97f ? x : std::copysign(0.97f + 0.03f * std::tanh((a - 0.97f) / 0.03f), x);
        };
        outInterleavedStereo[n * 2]     = safety(L * limiterGain_);
        outInterleavedStereo[n * 2 + 1] = safety(R * limiterGain_);

        if (std::abs(L) > maxL) maxL = std::abs(L);
        if (std::abs(R) > maxR) maxR = std::abs(R);
    }

    limiterReductionDb_.store(minLimiterGain < 0.9999f ? 20.f * std::log10(minLimiterGain) : 0.f, std::memory_order_relaxed);
    if (clipped) clipping_.store(true);
    else if (maxL < 0.85f && maxR < 0.85f) clipping_.store(false);

    peakL_.store(std::max(maxL, peakL_.load() * 0.82f));
    peakR_.store(std::max(maxR, peakR_.load() * 0.82f));
    for(int m=0;m<3;++m){
        const float v=std::isfinite(moduleMax[m])?moduleMax[m]:0.f;
        modulePeak_[size_t(m)].store(std::max(v,modulePeak_[size_t(m)].load(std::memory_order_relaxed)*0.82f),std::memory_order_relaxed);
    }
    for(int s=0;s<FX_SOURCE_COUNT;++s){
        const float v=std::isfinite(sourceMax[s])?sourceMax[s]:0.f;
        sourcePeak_[size_t(s)].store(std::max(v,sourcePeak_[size_t(s)].load(std::memory_order_relaxed)*0.82f),std::memory_order_relaxed);
    }
}

} // namespace SonusDSP