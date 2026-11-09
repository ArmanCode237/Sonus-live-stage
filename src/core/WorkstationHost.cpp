#include "WorkstationHost.h"

namespace NordDSP {

WorkstationHost::WorkstationHost() : NordPianoEngine() {
    registerAllParameters();
}

void WorkstationHost::registerAllParameters() {
    registry_.registerParam("maxVoices", 16.0f, 256.0f, [this](float v) { setMaxVoices(static_cast<int>(v)); });
    registry_.registerParam("transpose", -12.0f, 12.0f, [this](float v) { setTranspose(static_cast<int>(v)); });
    registry_.registerParam("octave", -2.0f, 2.0f, [this](float v) { setOctaveShift(static_cast<int>(v)); });
    registry_.registerParam("tune", -50.0f, 50.0f, [this](float v) { setMasterTuneCents(v); });
    registry_.registerParam("cutoff", 20.0f, 20000.0f, [this](float v) { setCutoffHz(v); });
    registry_.registerParam("resonance", 0.5f, 12.0f, [this](float v) { setResonanceQ(v); });
    registry_.registerParam("attack", 0.001f, 4.0f, [this](float v) { setAttackSec(v); });
    registry_.registerParam("decay", 0.02f, 8.0f, [this](float v) { setDecaySec(v); });
    registry_.registerParam("sustain", 0.0f, 1.0f, [this](float v) { setSustainLevel(v); });
    registry_.registerParam("release", 0.02f, 6.0f, [this](float v) { setReleaseSec(v); });
    registry_.registerParam("loopHold", 0.0f, 1.0f, [this](float v) { setLoopHold(v); });
    registry_.registerParam("volume", 0.0f, 1.5f, [this](float v) { setMasterGain(v); });
    registry_.registerParam("pan", -1.0f, 1.0f, [this](float v) { setPan(v); });
    registry_.registerParam("softRelease", 0.0f, 1.0f, [this](float v) { setSoftRelease(v >= 0.5f); });
    registry_.registerParam("touch", 0.0f, 2.0f, [this](float v) { setKbdTouch(static_cast<KbdTouch>(static_cast<int>(v))); });

    // EQ
    registry_.registerParam("eqEnabled", 0.0f, 1.0f, [this](float v) { setEqEnabled(v >= 0.5f); });
    registry_.registerParam("lowCut", 20.0f, 800.0f, [this](float v) { setLowCutHz(v); });
    registry_.registerParam("highCut", 1500.0f, 20000.0f, [this](float v) { setHighCutHz(v); });
    for (int b = 0; b < 5; ++b) {
        registry_.registerParam("eqGain" + std::to_string(b), -15.0f, 15.0f, [this, b](float v) { setEqBandGainDb(b, v); });
        registry_.registerParam("eqFreq" + std::to_string(b), 20.0f, 20000.0f, [this, b](float v) { setEqBandFreqHz(b, v); });
        registry_.registerParam("eqQ" + std::to_string(b), 0.25f, 12.0f, [this, b](float v) { setEqBandQ(b, v); });
    }

    // Modulaciones y Delay
    registry_.registerParam("chorusEnabled", 0.0f, 1.0f, [this](float v) { setChorusEnabled(v >= 0.5f); });
    registry_.registerParam("chorusRate", 0.1f, 6.0f, [this](float v) { setChorusRate(v); });
    registry_.registerParam("chorusDepth", 0.0f, 1.0f, [this](float v) { setChorusDepth(v); });
    registry_.registerParam("chorusMix", 0.0f, 1.0f, [this](float v) { setChorusMix(v); });

    registry_.registerParam("phaserEnabled", 0.0f, 1.0f, [this](float v) { setPhaserEnabled(v >= 0.5f); });
    registry_.registerParam("phaserRate", 0.1f, 6.0f, [this](float v) { setPhaserRate(v); });
    registry_.registerParam("phaserDepth", 0.0f, 1.0f, [this](float v) { setPhaserDepth(v); });
    registry_.registerParam("phaserFeedback", 0.0f, 0.85f, [this](float v) { setPhaserFeedback(v); });
    registry_.registerParam("phaserMix", 0.0f, 1.0f, [this](float v) { setPhaserMix(v); });

    registry_.registerParam("tremoloEnabled", 0.0f, 1.0f, [this](float v) { setTremoloEnabled(v >= 0.5f); });
    registry_.registerParam("tremoloRate", 0.5f, 14.0f, [this](float v) { setTremoloRate(v); });
    registry_.registerParam("tremoloDepth", 0.0f, 1.0f, [this](float v) { setTremoloDepth(v); });

    registry_.registerParam("delayEnabled", 0.0f, 1.0f, [this](float v) { setDelayEnabled(v >= 0.5f); });
    registry_.registerParam("delayTimeMs", 40.0f, 1500.0f, [this](float v) { setDelayTimeMs(v); });
    registry_.registerParam("delayFeedback", 0.0f, 0.92f, [this](float v) { setDelayFeedback(v); });
    registry_.registerParam("delayDamping", 0.0f, 1.0f, [this](float v) { setDelayDamping(v); });
    registry_.registerParam("delayMix", 0.0f, 1.0f, [this](float v) { setDelayMix(v); });
    registry_.registerParam("delayPingPong", 0.0f, 1.0f, [this](float v) { setDelayPingPong(v >= 0.5f); });

    // Reverb & Shimmer
    registry_.registerParam("reverbEnabled", 0.0f, 1.0f, [this](float v) { setReverbEnabled(v >= 0.5f); });
    registry_.registerParam("reverbType", 0.0f, 3.0f, [this](float v) { setReverbType(static_cast<int>(v)); });
    registry_.registerParam("reverbMix", 0.0f, 1.0f, [this](float v) { setReverbMix(v); });
    registry_.registerParam("reverbSize", 0.05f, 1.0f, [this](float v) { setReverbSize(v); });
    registry_.registerParam("reverbDamping", 0.0f, 1.0f, [this](float v) { setReverbDamping(v); });
    registry_.registerParam("reverbPreDelayMs", 0.0f, 240.0f, [this](float v) { setReverbPreDelayMs(v); });
    registry_.registerParam("reverbShimmer", 0.0f, 1.0f, [this](float v) { setReverbShimmer(v); });

    registry_.registerParam("shimmerShiftSt", -12.0f, 12.0f, [this](float v) { setShimmerShiftSt(v); });
    registry_.registerParam("shimmerFeedback", 0.0f, 0.985f, [this](float v) { setShimmerFeedback(v); });
    registry_.registerParam("shimmerDiffusion", 0.0f, 0.98f, [this](float v) { setShimmerDiffusion(v); });
    registry_.registerParam("shimmerLowCutHz", 20.0f, 1000.0f, [this](float v) { setShimmerLowCutHz(v); });
    registry_.registerParam("shimmerHighCutHz", 1000.0f, 20000.0f, [this](float v) { setShimmerHighCutHz(v); });
    registry_.registerParam("shimmerModRateHz", 0.05f, 5.0f, [this](float v) { setShimmerModRateHz(v); });
    registry_.registerParam("shimmerModDepth", 0.0f, 1.0f, [this](float v) { setShimmerModDepth(v); });
    registry_.registerParam("shimmerPitchMode", 0.0f, 4.0f, [this](float v) { setShimmerPitchMode(static_cast<int>(v)); });
    registry_.registerParam("shimmerReverbMode", 0.0f, 3.0f, [this](float v) { setShimmerReverbMode(static_cast<int>(v)); });
    registry_.registerParam("shimmerColorMode", 0.0f, 1.0f, [this](float v) { setShimmerColorMode(static_cast<int>(v)); });

    // Módulo 2: Synth 3-Layers
    registry_.registerParam("synthEnabled", 0.0f, 1.0f, [this](float v) { multiSynth_.synthEnabled = (v >= 0.5f); });
    registry_.registerParam("synthPresetId", 1.0f, 8.0f, [this](float v) { multiSynth_.loadBuiltInSynthPreset(static_cast<int>(v)); });
    registry_.registerParam("synthMasterVol", 0.0f, 1.5f, [this](float v) { multiSynth_.synthMasterVol = v; });
    registry_.registerParam("synthMacroCutoff", 0.1f, 2.0f, [this](float v) { multiSynth_.synthMacroCutoff = v; });

    for (int l = 0; l < 3; ++l) {
        std::string p = "L" + std::to_string(l + 1) + "_";
        registry_.registerParam(p + "en",   0.0f, 1.0f, [this, l](float v) { multiSynth_.layers[l].cfg.enabled = (v >= 0.5f); });
        registry_.registerParam(p + "wave", 0.0f, 5.0f, [this, l](float v) { multiSynth_.layers[l].cfg.waveType = std::clamp(static_cast<int>(v), 0, 5); });
        registry_.registerParam(p + "vol",  0.0f, 1.5f, [this, l](float v) { multiSynth_.layers[l].cfg.volume = v; });
        registry_.registerParam(p + "pan", -1.0f, 1.0f, [this, l](float v) { multiSynth_.layers[l].cfg.pan = v; });
        registry_.registerParam(p + "oct", -2.0f, 2.0f, [this, l](float v) { multiSynth_.layers[l].cfg.octave = std::clamp(static_cast<int>(v), -2, 2); });
        registry_.registerParam(p + "semi",-12.0f, 12.0f,[this, l](float v) { multiSynth_.layers[l].cfg.semitones = std::clamp(static_cast<int>(v), -12, 12); });
        registry_.registerParam(p + "det", -50.0f, 50.0f,[this, l](float v) { multiSynth_.layers[l].cfg.detuneCents = v; });
        registry_.registerParam(p + "cut",  40.0f, 20000.0f, [this, l](float v) { multiSynth_.layers[l].cfg.cutoffHz = v; });
        registry_.registerParam(p + "res",  0.5f, 12.0f, [this, l](float v) { multiSynth_.layers[l].cfg.resonanceQ = v; });
        registry_.registerParam(p + "atk",  0.001f, 4.0f,[this, l](float v) { multiSynth_.layers[l].cfg.attackSec = v; });
        registry_.registerParam(p + "dec",  0.02f, 6.0f, [this, l](float v) { multiSynth_.layers[l].cfg.decaySec = v; });
        registry_.registerParam(p + "sus",  0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.sustainLevel = v; });
        registry_.registerParam(p + "rel",  0.02f, 6.0f, [this, l](float v) { multiSynth_.layers[l].cfg.releaseSec = v; });
    }

    // Módulo 3: Órgano
    registry_.registerParam("organEnabled", 0.0f, 1.0f, [this](float v) { organModule_.organEnabled = (v >= 0.5f); });
    registry_.registerParam("organVolume", 0.0f, 1.5f, [this](float v) { organModule_.organVolume = v; });
    registry_.registerParam("organRotaryFast", 0.0f, 1.0f, [this](float v) { organModule_.organRotaryFast = (v >= 0.5f); });
    registry_.registerParam("organDrive", 0.0f, 1.0f, [this](float v) { organModule_.organDrive = v; });
    for (int d = 0; d < 9; ++d) {
        registry_.registerParam("drawbar" + std::to_string(d), 0.0f, 1.0f, [this, d](float v) {
            organModule_.drawbars[d] = std::clamp(v, 0.0f, 1.0f);
        });
    }
}

bool WorkstationHost::applyParamById(const std::string& id, float val, bool normalized) {
    return registry_.setParamValue(id, val, normalized);
}

} // namespace NordDSP