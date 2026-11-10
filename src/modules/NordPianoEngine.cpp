#include "NordPianoEngine.h"
#include <cmath>
#include <algorithm>

namespace NordDSP {

NordPianoEngine::NordPianoEngine() {
    const float defaultFreqs[5] = {60.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f};
    const float defaultQs[5]    = {0.9f,  1.0f,   1.0f,    1.0f,    0.9f};
    for (int i = 0; i < 5; ++i) {
        eqFreqHz_[i].store(defaultFreqs[i]);
        eqQ_[i].store(defaultQs[i]);
        eqGainDb_[i].store(0.0f);
    }
    for (auto& ks : keyStates_) ks.store(0);
    for (int i = 0; i < PIANO_LAYER_COUNT; ++i) {
        const float defaults[9] = {0.005f, 3.5f, 1.f, 0.35f, 0.65f, 0.f, 0.f, 20000.f, .707f};
        for (int k = 0; k < 9; ++k) pianoLayerSample_[i][k].store(defaults[k]);
    }
    for (int src = 0; src < FX_SOURCE_COUNT; ++src) {
        for (int bus = 0; bus < FX_BUS_COUNT; ++bus) {
            fxSends_[src][bus].store(1.0f);
            fxSendSmoothed_[src][bus] = 1.0f;
        }
    }
    multiSynth_.init(48000.0f);
    organModule_.init(48000.0f);
    modRack_.init(48000.0f);
    delayFx_.init(48000.0f);
    reverbFx_.init(48000.0f);
    shimmerReverbFx_.init(48000.0f);
    filterDirty_.store(true);
    eqDirty_.store(true);
    shimmerDirty_.store(true);
}

void NordPianoEngine::setSampleRate(double sr) {
    hostSampleRate_ = (sr > 8000.0) ? sr : 48000.0;
    float fsr = static_cast<float>(hostSampleRate_);
    multiSynth_.init(fsr);
    organModule_.init(fsr);
    // Preallocate 4096 stereo frames per source outside the real-time callback.
    // The buffers are reused for all subsequent render blocks.
    for (auto& stem : sourceAudio_) stem.assign(8192, 0.0f);
    svfFilter_.reset();
    modRack_.init(fsr);
    delayFx_.init(fsr);
    reverbFx_.init(fsr);
    shimmerReverbFx_.init(fsr);
    filterDirty_.store(true);
    eqDirty_.store(true);
    shimmerDirty_.store(true);
}

void NordPianoEngine::setInstrument(std::shared_ptr<const PianoInstrument> inst) {
    setLayerInstrument(0, std::move(inst));
}

void NordPianoEngine::setLayerInstrument(int layer, std::shared_ptr<const PianoInstrument> inst) {
    if (layer < 0 || layer >= PIANO_LAYER_COUNT) return;
    {
        std::lock_guard<std::mutex> lock(instPtrMutex_);
        if (layer == 0) instrument_ = std::move(inst);
        else extraPianoInstruments_[layer - 1] = std::move(inst);
    }
    // No se cortan las colas de piano ni las notas de synth/organ al cambiar banco.
    // Cada voz retiene su sampleBank hasta terminar su release.
    lastTriggeredStrokeIdx_.store(0);
}

void NordPianoEngine::setInstrument(const PianoInstrument* inst) {
    if (!inst) {
        setInstrument(std::shared_ptr<const PianoInstrument>(nullptr));
        return;
    }
    // Legacy caller-owned pointer (must outlive use). New layer API uses owning pointers.
    std::shared_ptr<const PianoInstrument> aliasPtr(inst, [](const PianoInstrument*) {});
    setInstrument(std::move(aliasPtr));
}

std::shared_ptr<const PianoInstrument> NordPianoEngine::getInstrument() const {
    return getLayerInstrument(0);
}

std::shared_ptr<const PianoInstrument> NordPianoEngine::getLayerInstrument(int layer) const {
    if (layer < 0 || layer >= PIANO_LAYER_COUNT) return {};
    std::lock_guard<std::mutex> lock(instPtrMutex_);
    return layer == 0 ? instrument_ : extraPianoInstruments_[layer - 1];
}

std::array<std::shared_ptr<const PianoInstrument>, PIANO_LAYER_COUNT> NordPianoEngine::getPianoInstruments() const {
    std::lock_guard<std::mutex> lock(instPtrMutex_);
    return {instrument_, extraPianoInstruments_[0], extraPianoInstruments_[1],
            extraPianoInstruments_[2], extraPianoInstruments_[3]};
}

void NordPianoEngine::setPianoLayerSampleParam(int layer, int param, float value) {
    if (layer < 0 || layer >= PIANO_LAYER_COUNT || param < 0 || param >= 9) return;
    const float lo[9] = {0.005f,0.02f,0.f,0.02f,0.f,-1.f,-100.f,40.f,0.5f};
    const float hi[9] = {5.f,12.f,1.f,10.f,1.f,1.f,100.f,20000.f,12.f};
    pianoLayerSample_[layer][param].store(std::clamp(value, lo[param], hi[param]));
}

void NordPianoEngine::applyPreset(const SynthPresetParams& p) {
    setPianoEnabled(p.pianoEnabled);
    setPianoVolume(p.pianoVolume);
    for (int i = 0; i < PIANO_LAYER_COUNT; ++i) {
        setPianoLayerEnabled(i, p.pianoLayerEnabled[i]);
        setPianoLayerVolume(i, p.pianoLayerVolume[i]);
    }
    for (int src = 0; src < FX_SOURCE_COUNT; ++src) {
        setFxSend(src, 0, p.fxSends[src].modulation);
        setFxSend(src, 1, p.fxSends[src].delay);
        setFxSend(src, 2, p.fxSends[src].reverb);
    }
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
        const float values[9] = {cfg.attackSec,cfg.decaySec,cfg.sustainLevel,cfg.releaseSec,
                                 cfg.loopHold,cfg.pan,cfg.fineTuneCents,cfg.cutoffHz,cfg.resonanceQ};
        for (int k = 0; k < 9; ++k) setPianoLayerSampleParam(layer,k,values[k]);
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

    multiSynth_.synthEnabled = p.synthEnabled;
    multiSynth_.synthPresetId = p.synthPresetId;
    multiSynth_.synthMasterVol = p.synthMasterVol;
    multiSynth_.synthMacroCutoff = p.synthMacroCutoff;
    multiSynth_.synthVoiceMode = p.synthVoiceMode;
    multiSynth_.synthPortamento = p.synthPortamento;
    multiSynth_.synthGlideMode = p.synthGlideMode;
    multiSynth_.synthSlide = p.synthSlide;
    for (int i = 0; i < 3; ++i) {
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

    organModule_.organEnabled = p.organEnabled;
    organModule_.organModel = p.organModel;
    organModule_.organVolume = p.organVolume;
    organModule_.organRotaryFast = p.organRotaryFast;
    organModule_.organDrive = p.organDrive;
    organModule_.rotaryMix = p.organRotaryMix;
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

void NordPianoEngine::setEqBandGainDb(int bandIdx, float db) {
    if (bandIdx >= 0 && bandIdx < 5) {
        eqGainDb_[bandIdx].store(std::clamp(db, -15.0f, 15.0f));
        eqDirty_.store(true);
    }
}

void NordPianoEngine::setEqBandFreqHz(int bandIdx, float hz) {
    if (bandIdx >= 0 && bandIdx < 5) {
        eqFreqHz_[bandIdx].store(std::clamp(hz, 20.0f, 20000.0f));
        eqDirty_.store(true);
    }
}

void NordPianoEngine::setEqBandQ(int bandIdx, float q) {
    if (bandIdx >= 0 && bandIdx < 5) {
        eqQ_[bandIdx].store(std::clamp(q, 0.25f, 12.0f));
        eqDirty_.store(true);
    }
}

SynthPresetParams NordPianoEngine::getCurrentParams() const {
    SynthPresetParams p;
    p.pianoEnabled = pianoEnabled_.load();
    p.pianoVolume = pianoVolume_.load();
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
    p.synthVoiceMode = multiSynth_.synthVoiceMode;
    p.synthPortamento = multiSynth_.synthPortamento;
    p.synthGlideMode = multiSynth_.synthGlideMode;
    p.synthSlide = multiSynth_.synthSlide;
    for (int i = 0; i < 3; ++i) {
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

void NordPianoEngine::getActiveKeyStates(std::array<uint8_t, 128>& outStates) const {
    for (size_t i = 0; i < 128; ++i) {
        outStates[i] = keyStates_[i].load(std::memory_order_relaxed);
    }
}

void NordPianoEngine::pushMidiEvent(uint8_t type, int d1, int d2) {
    size_t w = midiWriteIdx_.load(std::memory_order_relaxed);
    size_t nextW = (w + 1) % MIDI_QUEUE_SIZE;
    if (nextW != midiReadIdx_.load(std::memory_order_acquire)) {
        midiQueue_[w] = {type, d1, d2};
        midiWriteIdx_.store(nextW, std::memory_order_release);
    }
}

void NordPianoEngine::noteOn(int midiKey, int velocity) {
    if (velocity <= 0) {
        noteOff(midiKey);
        return;
    }
    lastMidiNote_.store(midiKey, std::memory_order_relaxed);
    lastMidiVel_.store(velocity, std::memory_order_relaxed);
    midiEventSeq_.fetch_add(1, std::memory_order_relaxed);
    pushMidiEvent(1, midiKey, velocity);
}

void NordPianoEngine::noteOff(int midiKey) {
    pushMidiEvent(2, midiKey, 0);
}

void NordPianoEngine::setSustainPedal(bool down) {
    sustainPedalState_.store(down, std::memory_order_relaxed);
    pushMidiEvent(3, down ? 1 : 0, 0);
}

void NordPianoEngine::allNotesOff() {
    pushMidiEvent(4, 0, 0);
}

void NordPianoEngine::triggerTestTone() {
    testToneFramesRemaining_.store(static_cast<uint32_t>(hostSampleRate_ * 0.6));
}

int NordPianoEngine::touchAttenuation(int velocity) const {
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

const StrokeSample* NordPianoEngine::selectStrokeFor(const PianoInstrument& inst, int midiKey, int velocity) const {
    if (inst.strokes.empty()) return nullptr;
    int att = touchAttenuation(velocity);

    int targetZone = -1;
    int minKeyDist = 999;
    for (const auto& s : inst.strokes) {
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
        if (s.zone == targetZone) {
            if (!pick) {
                pick = &s;
            } else if (s.switchPoint > 0 && s.switchPoint <= att) {
                pick = &s;
            } else if (s.switchPoint == 0 && s.layerIndex > 0 && velocity >= s.lovel && velocity <= s.hivel) {
                pick = &s;
            }
        }
    }
    return pick ? pick : &inst.strokes[0];
}

void NordPianoEngine::handleNoteOnInternal(
    const std::array<std::shared_ptr<const PianoInstrument>, PIANO_LAYER_COUNT>& instruments,
    int rawMidiKey, int velocity) {
    int midiKey = std::clamp(rawMidiKey + transposeSemis_.load() + octaveShift_.load() * 12, 0, 127);

    // Siempre propagar NoteOn a Synth y Órgano aunque no haya piano cargado o el piano esté desactivado
    multiSynth_.noteOn(rawMidiKey, midiKey, velocity);
    organModule_.noteOn(rawMidiKey, midiKey, velocity);

    if (!pianoEnabled_.load()) return;
    for (int layer = 0; layer < PIANO_LAYER_COUNT; ++layer) {
        if (!pianoLayerEnabled_[layer].load() || !instruments[layer] || instruments[layer]->strokes.empty()) continue;
        startPianoVoice(instruments[layer], layer, rawMidiKey, midiKey, velocity);
    }
}

void NordPianoEngine::startPianoVoice(const std::shared_ptr<const PianoInstrument>& inst,
                                     int layer, int rawMidiKey, int midiKey, int velocity) {
    const StrokeSample* stroke = selectStrokeFor(*inst, midiKey, velocity);
    if (!stroke || stroke->frames < 4) return;

    for (size_t idx = 0; idx < inst->strokes.size(); ++idx) {
        if (&inst->strokes[idx] == stroke) {
            lastTriggeredStrokeIdx_.store(static_cast<int>(idx), std::memory_order_relaxed);
            break;
        }
    }

    const size_t maxAllowed = static_cast<size_t>(std::clamp(maxVoicesLimit_.load(), 16, static_cast<int>(MAX_VOICES)));

    Voice* oldestSameKey = nullptr;
    int sameKeyCount = 0;
    for (size_t i = 0; i < maxAllowed; ++i) {
        auto& v = voices_[i];
        if (v.active && v.midiKey == rawMidiKey && v.pianoLayer == layer && v.stage != EnvStage::FastStealFade) {
            sameKeyCount++;
            if (!sustainPedalDown_) {
                v.keyHeld = false;
                v.sustainedByPedal = false;
                v.stage = EnvStage::FastStealFade;
            } else {
                v.velocityGain *= 0.78f;
                if (!oldestSameKey || v.ageOrder < oldestSameKey->ageOrder) {
                    oldestSameKey = &v;
                }
            }
        }
    }
    if (sustainPedalDown_ && sameKeyCount >= 4 && oldestSameKey) {
        oldestSameKey->keyHeld = false;
        oldestSameKey->sustainedByPedal = false;
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

    double cents = 100.0 * (midiKey - stroke->actualRootMidi) + stroke->stretchCents + masterTuneCents_.load();
    double pitchRatio = std::pow(2.0, cents / 1200.0);

    int att = touchAttenuation(velocity);
    int netAtt = std::max(0, att - (stroke->velocityFrom / 2));
    float gainDb = -static_cast<float>(netAtt) * 0.18f;
    float velGain = std::pow(10.0f, gainDb / 20.0f) * stroke->levelGainLinear * 0.45f;

    float safeDecayCoef = std::clamp(stroke->decayCoef, 0.99985f, 0.999998f);
    float baseLoopDecay = std::pow(safeDecayCoef, static_cast<float>(NSP_ENVELOPE_RATE / hostSampleRate_));
    double srcRate = (stroke->sampleRate > 0) ? static_cast<double>(stroke->sampleRate) : NSP_SAMPLE_RATE;

    target->sampleBank = inst;
    target->pianoLayer = layer;
    target->active = true;
    target->midiKey = rawMidiKey;
    target->playedMidiKey = midiKey;
    target->velocity = velocity;
    target->keyHeld = true;
    target->sustainedByPedal = sustainPedalDown_;
    target->stage = EnvStage::Attack;
    target->ageOrder = ++voiceCounter_;
    target->sample = stroke;
    target->pos = static_cast<double>(stroke->startFrameOffset);
    target->step = (pitchRatio * srcRate) / hostSampleRate_;
    target->velocityGain = velGain;
    target->sampleEnv = 1.0f;
    target->adsrLevel = 0.0f;
    target->baseLoopDecayPerSample = baseLoopDecay;
    target->inLoop = false;
}

void NordPianoEngine::handleNoteOffInternal(int rawMidiKey) {
    multiSynth_.noteOff(rawMidiKey, sustainPedalDown_);
    organModule_.noteOff(rawMidiKey, sustainPedalDown_);

    for (auto& v : voices_) {
        if (v.active && v.midiKey == rawMidiKey && v.keyHeld) {
            v.keyHeld = false;
            if (sustainPedalDown_) {
                v.sustainedByPedal = true;
            } else if (v.stage != EnvStage::FastStealFade) {
                v.sustainedByPedal = false;
                v.stage = EnvStage::Release;
            }
        }
    }
}

void NordPianoEngine::handleSustainInternal(bool down) {
    if (sustainPedalDown_ == down) return;
    sustainPedalDown_ = down;

    multiSynth_.setSustain(down);
    organModule_.setSustain(down);

    if (down) {
        for (auto& v : voices_) {
            if (v.active && v.keyHeld) {
                v.sustainedByPedal = true;
            }
        }
    } else {
        for (auto& v : voices_) {
            if (v.active && !v.keyHeld && v.sustainedByPedal) {
                v.sustainedByPedal = false;
                if (v.stage != EnvStage::FastStealFade) {
                    v.stage = EnvStage::Release;
                }
            }
        }
    }
}

void NordPianoEngine::render(float* outInterleavedStereo, unsigned int numFrames) {
    std::fill(outInterleavedStereo, outInterleavedStereo + numFrames * 2, 0.0f);

    const auto pianoInstruments = getPianoInstruments();
    const float sr = static_cast<float>(hostSampleRate_);

    size_t r = midiReadIdx_.load(std::memory_order_relaxed);
    const size_t w = midiWriteIdx_.load(std::memory_order_acquire);
    while (r != w) {
        const QueuedMidiEvent ev = midiQueue_[r];
        r = (r + 1) % MIDI_QUEUE_SIZE;
        if (ev.type == 1) handleNoteOnInternal(pianoInstruments, ev.data1, ev.data2);
        else if (ev.type == 2) handleNoteOffInternal(ev.data1);
        else if (ev.type == 3) handleSustainInternal(ev.data1 != 0);
        else if (ev.type == 4) {
            for (auto& v : voices_) { v.active = false; v.stage = EnvStage::Idle; }
            multiSynth_.allNotesOff();
            organModule_.allNotesOff();
        }
    }
    midiReadIdx_.store(r, std::memory_order_release);

    if (filterDirty_.exchange(false)) {
        svfFilter_.setParams(sr, cutoffHz_.load(), resonanceQ_.load());
    }
    if (eqDirty_.exchange(false)) {
        EqBandParams bands[5];
        for (int i = 0; i < 5; ++i) {
            bands[i].freqHz = eqFreqHz_[i].load();
            bands[i].q      = eqQ_[i].load();
            bands[i].gainDb = eqGainDb_[i].load();
        }
        masterEq_.update(sr, eqEnabled_.load(), lowCutHz_.load(), highCutHz_.load(), bands);
    }

    if (shimmerDirty_.exchange(false)) {
        shimmerReverbFx_.setPreDelayMs(reverbPreDelayMs_.load());
        shimmerReverbFx_.setParams(
            reverbMix_.load(),
            shimmerShiftSt_.load(),
            shimmerFeedback_.load(),
            shimmerDiffusion_.load(),
            reverbSize_.load(),
            shimmerLowCutHz_.load(),
            shimmerHighCutHz_.load(),
            shimmerModRateHz_.load(),
            shimmerModDepth_.load(),
            static_cast<ShimmerReverbEngine::PitchMode>(shimmerPitchMode_.load()),
            static_cast<ShimmerReverbEngine::ReverbMode>(shimmerReverbMode_.load()),
            static_cast<ShimmerReverbEngine::ColorMode>(shimmerColorMode_.load())
        );
    }

    const bool softRel = softRelease_.load();
    const float fastStealStep = 1.0f / (0.012f * sr);
    const size_t maxAllowed = static_cast<size_t>(std::clamp(maxVoicesLimit_.load(), 16, static_cast<int>(MAX_VOICES)));

    int activeCount = 0;
    uint8_t localKeyMap[128] = {0};

    // Independent stereo stems for five piano layers, three synth layers, and organ.
    // Keep these vectors allocated between callbacks to avoid heap work at steady state.
    for (auto& stem : sourceAudio_) {
        if (stem.size() < numFrames * 2) stem.resize(numFrames * 2);
        std::fill_n(stem.data(), numFrames * 2, 0.0f);
    }
    const std::array<float*, 3> synthStems = {
        sourceAudio_[PIANO_LAYER_COUNT + 0].data(),
        sourceAudio_[PIANO_LAYER_COUNT + 1].data(),
        sourceAudio_[PIANO_LAYER_COUNT + 2].data()
    };
    const float midiBend = pitchBend_.load(std::memory_order_relaxed);
    const float midiMod = modWheel_.load(std::memory_order_relaxed);
    const double tremPhaseStep = 2.0 * 3.141592653589793 * 5.2 / hostSampleRate_;
    for (auto& synthLayer : multiSynth_.layers) {
        synthLayer.performancePitchBendSemis = midiBend * 2.f;
        synthLayer.performanceModWheel = midiMod;
    }
    organModule_.pitchBendSemis = midiBend * 2.f;
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
            const bool voiceAudible = pianoEnabled_.load() && pianoLayerEnabled_[v.pianoLayer].load();
            const float layerGain = pianoVolume_.load() * pianoLayerVolume_[v.pianoLayer].load();
            activeCount++;

            if (v.midiKey >= 0 && v.midiKey < 128) {
                if (v.keyHeld) localKeyMap[v.midiKey] = 1;
                else if (v.sustainedByPedal && localKeyMap[v.midiKey] == 0 && (v.adsrLevel * v.sampleEnv) > 0.02f) {
                    localKeyMap[v.midiKey] = 2;
                }
            }

            const StrokeSample& s = *v.sample;
            const uint32_t last = s.frames - 1;
            const bool useLoop = s.loop.hasLoop && (s.loop.length >= 16);
            const uint32_t loopStart = s.loop.start;
            const uint32_t loopLen = s.loop.length;
            const double loopEndD = useLoop ? static_cast<double>(loopStart + loopLen) : 1e18;

            float damperSec = softRel ? s.softDamperT60Seconds : s.damperT60Seconds;
            if (v.playedMidiKey >= voiceInst.undampedFromKey) {
                damperSec = std::max(damperSec, 0.85f);
            }
            const int li = std::clamp(v.pianoLayer, 0, PIANO_LAYER_COUNT-1);
            const float layerAtk = std::max(0.005f, pianoLayerSample_[li][0].load());
            const float layerDec = std::max(0.02f, pianoLayerSample_[li][1].load());
            const float layerSus = std::clamp(pianoLayerSample_[li][2].load(), 0.f, 1.f);
            const float layerRel = std::max(0.02f, pianoLayerSample_[li][3].load());
            const float layerHold = std::clamp(pianoLayerSample_[li][4].load(), 0.f, 1.f);
            const float layerPan = std::clamp(pianoLayerSample_[li][5].load(), -1.f, 1.f);
            const float layerTune = pianoLayerSample_[li][6].load();
            const float attackStep = 1.f / (layerAtk * sr);
            const float decayCoef = std::exp(-4.6f / (layerDec * sr));
            const float sustainLvl = layerSus;
            const float loopDecayExponent = (layerHold >= 0.99f) ? 0.f : std::pow((1.f-layerHold)*1.6f,1.5f);
            const float panGainL = std::sqrt(1.f-std::max(0.f,layerPan));
            const float panGainR = std::sqrt(1.f+std::min(0.f,layerPan));
            const double pitchMultiplier = std::pow(2.0, (midiBend*200.f+layerTune)/1200.0);
            float effectiveRelSec = std::max(0.02f, damperSec * (layerRel / 0.35f));
            const float releaseCoef = std::exp(-6.907755f / (effectiveRelSec * sr));

            const float effectiveLoopDecay = (loopDecayExponent <= 0.0f)
                ? 1.0f
                : std::pow(v.baseLoopDecayPerSample, loopDecayExponent);

            auto wrapIdx = [&](int64_t idx) -> uint32_t {
                if (idx < 0) return 0u;
                uint32_t u = static_cast<uint32_t>(idx);
                if (useLoop && v.inLoop && u >= loopStart + loopLen) {
                    return loopStart + ((u - loopStart) % loopLen);
                }
                return std::min(last, u);
            };

            for (unsigned int n = 0; n < numFrames; ++n) {
                if (useLoop && v.pos >= loopEndD) {
                    v.pos = loopStart + std::fmod(v.pos - loopStart, static_cast<double>(loopLen));
                    v.inLoop = true;
                }

                uint32_t i = static_cast<uint32_t>(v.pos);
                if (!useLoop && i + 2 >= s.frames) {
                    v.active = false;
                    v.stage = EnvStage::Idle;
                    break;
                }

                switch (v.stage) {
                    case EnvStage::Attack:
                        v.adsrLevel += attackStep;
                        if (v.adsrLevel >= 1.0f) {
                            v.adsrLevel = 1.0f;
                            v.stage = EnvStage::Decay;
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
                        v.adsrLevel -= fastStealStep;
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
                uint32_t km1 = wrapIdx(static_cast<int64_t>(i) - 1);
                uint32_t k0  = wrapIdx(static_cast<int64_t>(i));
                uint32_t k1  = wrapIdx(static_cast<int64_t>(i) + 1);
                uint32_t k2  = wrapIdx(static_cast<int64_t>(i) + 2);

                float xm1L = s.audioL[km1], x0L = s.audioL[k0], x1L = s.audioL[k1], x2L = s.audioL[k2];
                float valL = x0L + 0.5f * t * (x1L - xm1L + t * (2.0f * xm1L - 5.0f * x0L + 4.0f * x1L - x2L + t * (3.0f * (x0L - x1L) + x2L - xm1L)));

                float xm1R = s.audioR[km1], x0R = s.audioR[k0], x1R = s.audioR[k1], x2R = s.audioR[k2];
                float valR = x0R + 0.5f * t * (x1R - xm1R + t * (2.0f * xm1R - 5.0f * x0R + 4.0f * x1R - x2R + t * (3.0f * (x0R - x1R) + x2R - xm1R)));

                const float tremGain = 1.0f - midiMod * 0.12f *
                    (0.5f + 0.5f * std::sin((v.pos * v.step * tremPhaseStep)));
                float voiceGain = voiceAudible ? v.velocityGain * v.sampleEnv * v.adsrLevel * layerGain * tremGain : 0.0f;
                sourceAudio_[v.pianoLayer][n * 2]     += valL * voiceGain * panGainL;
                sourceAudio_[v.pianoLayer][n * 2 + 1] += valR * voiceGain * panGainR;

                v.pos += v.step * pitchMultiplier;
                if (useLoop && v.pos >= loopStart) {
                    v.inLoop = true;
                }
                if (v.inLoop) {
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

    // Per-piano-layer resonant low-pass: processing happens on isolated stems, BEFORE
    // routing to AUX sends. State persists per layer/channel across audio blocks.
    // Coefficients are constant per block to avoid zipper noise on each individual note.
    for (int layer = 0; layer < PIANO_LAYER_COUNT; ++layer) {
        const float hz = std::clamp(pianoLayerSample_[layer][7].load(), 40.f, sr * .45f);
        const float q = std::clamp(pianoLayerSample_[layer][8].load(), .5f, 12.f);
        if (hz >= 19500.f && q <= .75f) { pianoFilterStates_[layer] = {}; continue; } // transparent legacy default
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
            stem[x] = v2L; stem[x+1] = v2R;
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
    const float delayTime = delayTimeMs_.load();
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

    float maxL = 0.0f, maxR = 0.0f;
    bool clipped = false;
    const float sendCoeff = 1.0f - std::exp(-1.0f / (0.012f * sr));

    for (unsigned int n = 0; n < numFrames; ++n) {
        float dryL = outInterleavedStereo[n * 2];  // test tone, if active
        float dryR = outInterleavedStereo[n * 2 + 1];
        float fxIn[FX_BUS_COUNT][2]{};
        for (int src = 0; src < FX_SOURCE_COUNT; ++src) {
            const float inL = sourceAudio_[src][n * 2];
            const float inR = sourceAudio_[src][n * 2 + 1];
            dryL += inL; dryR += inR;
            for (int bus = 0; bus < FX_BUS_COUNT; ++bus) {
                float& smoothed = fxSendSmoothed_[src][bus];
                smoothed += (fxSends_[src][bus].load(std::memory_order_relaxed) - smoothed) * sendCoeff;
                fxIn[bus][0] += inL * smoothed;
                fxIn[bus][1] += inR * smoothed;
            }
        }

        float L = dryL, R = dryR;
        if (useSvf) svfFilter_.processStereo(L, R);
        masterEq_.processStereo(L, R);

        // Parallel wet-only auxiliary buses; original samples are present ONCE in dry.
        float modL = fxIn[0][0], modR = fxIn[0][1];
        modRack_.processStereo(modL, modR,
                               tremOn, tremRate, tremDepth,
                               phaserOn, phaserRate, phaserDepth, phaserFb, phaserMix,
                               chorusOn, chorusRate, chorusDepth, chorusMix,
                               chorusVoices, chorusWidth, chorusFeedback, chorusTone, chorusDelay);
        L += modL - fxIn[0][0]; R += modR - fxIn[0][1];

        float dlyL = fxIn[1][0], dlyR = fxIn[1][1];
        delayFx_.processStereo(dlyL, dlyR, delayOn, delayTime, delayFb,
                               delayDamp, delayMix, delayPP);
        L += dlyL - fxIn[1][0]; R += dlyR - fxIn[1][1];

        if (revOn) {
            float revL = fxIn[2][0], revR = fxIn[2][1];
            if (revType == 3) shimmerReverbFx_.processStereo(revL, revR);
            else reverbFx_.processStereo(revL, revR, true, revType, revMix,
                                         revSize, revDamp, revPre, revShimmer);
            // Each reverb implementation mixes its own dry signal. Remove
            // precisely that dry component so the send remains wet-only and
            // never cancels part of the workstation's main dry path.
            const float revDryGain = revType == 3
                ? std::cos(revMix * PI_F * 0.42f)
                : 1.0f - revMix * 0.28f;
            L += revL - fxIn[2][0] * revDryGain;
            R += revR - fxIn[2][1] * revDryGain;
        }

        L *= gainL;
        R *= gainR;

        if (std::abs(L) > 0.99f || std::abs(R) > 0.99f) clipped = true;

        // Soft limiting protects stacked layers from harsh digital clipping.
        auto softLimit = [](float x) {
            const float a = std::abs(x);
            return a <= 0.85f ? x : std::copysign(0.85f + 0.15f * std::tanh((a - 0.85f) / 0.15f), x);
        };
        outInterleavedStereo[n * 2]     = softLimit(L);
        outInterleavedStereo[n * 2 + 1] = softLimit(R);

        if (std::abs(L) > maxL) maxL = std::abs(L);
        if (std::abs(R) > maxR) maxR = std::abs(R);
    }

    if (clipped) clipping_.store(true);
    else if (maxL < 0.85f && maxR < 0.85f) clipping_.store(false);

    peakL_.store(std::max(maxL, peakL_.load() * 0.82f));
    peakR_.store(std::max(maxR, peakR_.load() * 0.82f));
}

} // namespace NordDSP