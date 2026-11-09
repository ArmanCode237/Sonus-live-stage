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
    {
        std::lock_guard<std::mutex> lock(instPtrMutex_);
        instrument_ = std::move(inst);
    }
    lastTriggeredStrokeIdx_.store(0);
    pushMidiEvent(4, 0, 0);
}

void NordPianoEngine::setInstrument(const PianoInstrument* inst) {
    if (!inst) {
        setInstrument(std::shared_ptr<const PianoInstrument>(nullptr));
        return;
    }
    std::shared_ptr<const PianoInstrument> aliasPtr(inst, [](const PianoInstrument*) {});
    setInstrument(std::move(aliasPtr));
}

std::shared_ptr<const PianoInstrument> NordPianoEngine::getInstrument() const {
    std::lock_guard<std::mutex> lock(instPtrMutex_);
    return instrument_;
}

void NordPianoEngine::applyPreset(const SynthPresetParams& p) {
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

void NordPianoEngine::handleNoteOnInternal(const PianoInstrument& inst, int rawMidiKey, int velocity) {
    int midiKey = std::clamp(rawMidiKey + transposeSemis_.load() + octaveShift_.load() * 12, 0, 127);

    const StrokeSample* stroke = selectStrokeFor(inst, midiKey, velocity);
    if (!stroke || stroke->frames < 4) return;

    for (size_t idx = 0; idx < inst.strokes.size(); ++idx) {
        if (&inst.strokes[idx] == stroke) {
            lastTriggeredStrokeIdx_.store(static_cast<int>(idx), std::memory_order_relaxed);
            break;
        }
    }

    const size_t maxAllowed = static_cast<size_t>(std::clamp(maxVoicesLimit_.load(), 16, static_cast<int>(MAX_VOICES)));

    Voice* oldestSameKey = nullptr;
    int sameKeyCount = 0;
    for (size_t i = 0; i < maxAllowed; ++i) {
        auto& v = voices_[i];
        if (v.active && v.midiKey == rawMidiKey && v.stage != EnvStage::FastStealFade) {
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

    target->active = true;
    target->midiKey = rawMidiKey;
    target->playedMidiKey = midiKey;
    target->velocity = velocity;
    target->keyHeld = true;
    // CORRECCIÓN: sustainedByPedal depende SOLO de si el pedal físico está presionado
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
    for (auto& v : voices_) {
        if (v.active && v.midiKey == rawMidiKey && v.keyHeld) {
            v.keyHeld = false;
            // CORRECCIÓN: Si el pedal NO está presionado, pasar siempre a Release (incluso en las últimas 2 octavas)
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

    std::shared_ptr<const PianoInstrument> inst = getInstrument();
    const float sr = static_cast<float>(hostSampleRate_);

    size_t r = midiReadIdx_.load(std::memory_order_relaxed);
    const size_t w = midiWriteIdx_.load(std::memory_order_acquire);
    while (r != w) {
        const QueuedMidiEvent ev = midiQueue_[r];
        r = (r + 1) % MIDI_QUEUE_SIZE;
        if (ev.type == 1 && inst) handleNoteOnInternal(*inst, ev.data1, ev.data2);
        else if (ev.type == 2) handleNoteOffInternal(ev.data1);
        else if (ev.type == 3) handleSustainInternal(ev.data1 != 0);
        else if (ev.type == 4) {
            for (auto& v : voices_) { v.active = false; v.stage = EnvStage::Idle; }
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

    const float attackSec = std::max(0.001f, attackSec_.load());
    const float decaySec = std::max(0.005f, decaySec_.load());
    const float sustainLvl = std::clamp(sustainLevel_.load(), 0.0f, 1.0f);
    const float userRelSec = std::max(0.005f, releaseSec_.load());
    const float loopHold = std::clamp(loopHold_.load(), 0.0f, 1.0f);
    const bool softRel = softRelease_.load();

    const float attackStep = 1.0f / (attackSec * sr);
    const float decayCoef = std::exp(-4.6f / (decaySec * sr));
    const float fastStealStep = 1.0f / (0.012f * sr);

    const float loopDecayExponent = (loopHold >= 0.99f) ? 0.0f : std::pow((1.0f - loopHold) * 1.6f, 1.5f);
    const size_t maxAllowed = static_cast<size_t>(std::clamp(maxVoicesLimit_.load(), 16, static_cast<int>(MAX_VOICES)));

    int activeCount = 0;
    uint8_t localKeyMap[128] = {0};

    if (inst && !inst->strokes.empty()) {
        for (size_t vIdx = 0; vIdx < maxAllowed; ++vIdx) {
            auto& v = voices_[vIdx];
            if (!v.active || !v.sample) continue;
            activeCount++;

            if (v.midiKey >= 0 && v.midiKey < 128) {
                if (v.keyHeld) localKeyMap[v.midiKey] = 1; // 1 = presionada con el dedo
                else if (v.sustainedByPedal && localKeyMap[v.midiKey] == 0 && (v.adsrLevel * v.sampleEnv) > 0.02f) {
                    localKeyMap[v.midiKey] = 2; // 2 = sostenida por pedal
                }
            }

            const StrokeSample& s = *v.sample;
            const uint32_t last = s.frames - 1;
            const bool useLoop = s.loop.hasLoop && (s.loop.length >= 16);
            const uint32_t loopStart = s.loop.start;
            const uint32_t loopLen = s.loop.length;
            const double loopEndD = useLoop ? static_cast<double>(loopStart + loopLen) : 1e18;

            float damperSec = softRel ? s.softDamperT60Seconds : s.damperT60Seconds;
            if (v.playedMidiKey >= inst->undampedFromKey) {
                damperSec = std::max(damperSec, 0.85f); // Resonancia natural de cuerdas agudas al soltar la tecla
            }
            float effectiveRelSec = std::max(0.02f, damperSec * (userRelSec / 0.35f));
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

                float voiceGain = v.velocityGain * v.sampleEnv * v.adsrLevel;
                outInterleavedStereo[n * 2]     += valL * voiceGain;
                outInterleavedStereo[n * 2 + 1] += valR * voiceGain;

                v.pos += v.step;
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

    for (unsigned int n = 0; n < numFrames; ++n) {
        float L = outInterleavedStereo[n * 2];
        float R = outInterleavedStereo[n * 2 + 1];

        if (useSvf) svfFilter_.processStereo(L, R);
        masterEq_.processStereo(L, R);

        modRack_.processStereo(L, R,
                               tremOn, tremRate, tremDepth,
                               phaserOn, phaserRate, phaserDepth, phaserFb, phaserMix,
                               chorusOn, chorusRate, chorusDepth, chorusMix);

        delayFx_.processStereo(L, R, delayOn, delayTime, delayFb, delayDamp, delayMix, delayPP);
        if (revOn) {
            if (revType == 3) {
                shimmerReverbFx_.processStereo(L, R);
            } else {
                reverbFx_.processStereo(L, R, true, revType, revMix, revSize, revDamp, revPre, revShimmer);
            }
        }

        L *= gainL;
        R *= gainR;

        if (std::abs(L) > 0.99f || std::abs(R) > 0.99f) clipped = true;

        outInterleavedStereo[n * 2]     = std::clamp(L, -1.0f, 1.0f);
        outInterleavedStereo[n * 2 + 1] = std::clamp(R, -1.0f, 1.0f);

        if (std::abs(L) > maxL) maxL = std::abs(L);
        if (std::abs(R) > maxR) maxR = std::abs(R);
    }

    if (clipped) clipping_.store(true);
    else if (maxL < 0.85f && maxR < 0.85f) clipping_.store(false);

    peakL_.store(std::max(maxL, peakL_.load() * 0.82f));
    peakR_.store(std::max(maxR, peakR_.load() * 0.82f));
}

} // namespace NordDSP