#include "WorkstationHost.h"
#include "../modules/NordSynthModule.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>

using json = nlohmann::json;

namespace NordDSP {

WorkstationHost::WorkstationHost() : NordPianoEngine() {
    registerAllParameters();
}

void WorkstationHost::registerAllParameters() {
    registry_.registerParam("pianoEnabled", 0.0f, 1.0f, [this](float v) { setPianoEnabled(v >= 0.5f); });
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
        registry_.registerParam(p + "en",     0.0f, 1.0f,    [this, l](float v) { multiSynth_.layers[l].cfg.enabled = (v >= 0.5f); });
        registry_.registerParam(p + "wave",   0.0f, 9.0f,    [this, l](float v) { multiSynth_.layers[l].cfg.waveType = std::clamp(static_cast<int>(v), 0, 9); });
        registry_.registerParam(p + "vol",    0.0f, 1.5f,    [this, l](float v) { multiSynth_.layers[l].cfg.volume = v; });
        registry_.registerParam(p + "pan",   -1.0f, 1.0f,    [this, l](float v) { multiSynth_.layers[l].cfg.pan = v; });
        registry_.registerParam(p + "oct",   -2.0f, 2.0f,    [this, l](float v) { multiSynth_.layers[l].cfg.octave = std::clamp(static_cast<int>(v), -2, 2); });
        registry_.registerParam(p + "semi", -12.0f, 12.0f,   [this, l](float v) { multiSynth_.layers[l].cfg.semitones = std::clamp(static_cast<int>(v), -12, 12); });
        registry_.registerParam(p + "det",  -50.0f, 50.0f,   [this, l](float v) { multiSynth_.layers[l].cfg.detuneCents = v; });
        registry_.registerParam(p + "cut",   40.0f, 20000.0f,[this, l](float v) { multiSynth_.layers[l].cfg.cutoffHz = v; });
        registry_.registerParam(p + "res",    0.5f, 12.0f,   [this, l](float v) { multiSynth_.layers[l].cfg.resonanceQ = v; });
        registry_.registerParam(p + "atk",    0.001f, 4.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.attackSec = v; });
        registry_.registerParam(p + "dec",    0.02f, 6.0f,   [this, l](float v) { multiSynth_.layers[l].cfg.decaySec = v; });
        registry_.registerParam(p + "sus",    0.0f, 1.0f,    [this, l](float v) { multiSynth_.layers[l].cfg.sustainLevel = v; });
        registry_.registerParam(p + "rel",    0.02f, 6.0f,   [this, l](float v) { multiSynth_.layers[l].cfg.releaseSec = v; });

        registry_.registerParam(p + "unison",   1.0f, 8.0f, [this, l](float v) { multiSynth_.layers[l].cfg.unisonVoices = std::clamp(static_cast<int>(v), 1, 8); });
        registry_.registerParam(p + "uniDet",   0.0f, 50.0f, [this, l](float v) { multiSynth_.layers[l].cfg.unisonDetune = v; });
        registry_.registerParam(p + "uniSprd",  0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.unisonSpread = v; });
        registry_.registerParam(p + "phase",    0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.phaseOffset = v; });
        registry_.registerParam(p + "keyTrk",   0.0f, 2.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.keyTrack = v; });
        registry_.registerParam(p + "velCut",   0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.velToCutoff = v; });
        registry_.registerParam(p + "velAmp",   0.0f, 2.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.velToAmp = v; });
        registry_.registerParam(p + "wtIdx",    0.0f, 63.0f, [this, l](float v) { multiSynth_.layers[l].cfg.wavetableIdx = std::clamp(static_cast<int>(v), 0, 63); });
        registry_.registerParam(p + "filtType", 0.0f, 2.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.filterType = std::clamp(static_cast<int>(v), 0, 2); });
        registry_.registerParam(p + "filtEnv", -1.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.filterEnvAmt = v; });
        registry_.registerParam(p + "filtKT",   0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.filterKeyTrack = v; });
        registry_.registerParam(p + "fAtk",     0.001f, 4.0f,[this, l](float v) { multiSynth_.layers[l].cfg.filtAttackSec = v; });
        registry_.registerParam(p + "fDec",     0.005f, 10.0f,[this, l](float v) { multiSynth_.layers[l].cfg.filtDecaySec = v; });
        registry_.registerParam(p + "fSus",     0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.filtSustainLvl = v; });
        registry_.registerParam(p + "fRel",     0.005f, 10.0f,[this, l](float v) { multiSynth_.layers[l].cfg.filtReleaseSec = v; });
        registry_.registerParam(p + "saw", 0.0f, 1.0f, [this, l](float v) { multiSynth_.layers[l].cfg.sawLevel = v; });
        registry_.registerParam(p + "pulse", 0.0f, 1.0f, [this, l](float v) { multiSynth_.layers[l].cfg.pulseLevel = v; });
        registry_.registerParam(p + "pw", 0.05f, 0.95f, [this, l](float v) { multiSynth_.layers[l].cfg.pulseWidth = v; });
        registry_.registerParam(p + "pwmDepth", 0.0f, 0.45f, [this, l](float v) { multiSynth_.layers[l].cfg.pwmDepth = v; });
        registry_.registerParam(p + "pwmRate", 0.05f, 12.0f, [this, l](float v) { multiSynth_.layers[l].cfg.pwmRateHz = v; });
        registry_.registerParam(p + "sub", 0.0f, 1.0f, [this, l](float v) { multiSynth_.layers[l].cfg.subLevel = v; });
        registry_.registerParam(p + "noise", 0.0f, 1.0f, [this, l](float v) { multiSynth_.layers[l].cfg.noiseLevel = v; });
        registry_.registerParam(p + "osc2", 0.0f, 1.0f, [this, l](float v) { multiSynth_.layers[l].cfg.osc2Level = v; });
        registry_.registerParam(p + "osc2Det", -50.0f, 50.0f, [this, l](float v) { multiSynth_.layers[l].cfg.osc2Detune = v; });
        registry_.registerParam(p + "osc2Semi", -12.0f, 12.0f, [this, l](float v) { multiSynth_.layers[l].cfg.osc2Semitones = std::clamp(static_cast<int>(v), -12, 12); });
        registry_.registerParam(p + "osc2Wave", 0.0f, 2.0f, [this, l](float v) { multiSynth_.layers[l].cfg.osc2Wave = std::clamp(static_cast<int>(v), 0, 2); });
        registry_.registerParam(p + "hpf", 20.0f, 6000.0f, [this, l](float v) { multiSynth_.layers[l].cfg.highpassHz = v; });
        registry_.registerParam(p + "drive", 0.0f, 1.0f, [this, l](float v) { multiSynth_.layers[l].cfg.drive = v; });
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

    registry_.registerParam("synth-unison-voices", 1.0f, 8.0f, [this](float v) {
        int u = std::clamp(static_cast<int>(v), 1, 8);
        for (auto& l : multiSynth_.layers) l.cfg.unisonVoices = u;
    });
    registry_.registerParam("synth-unison-detune", 0.0f, 50.0f, [this](float v) {
        for (auto& l : multiSynth_.layers) l.cfg.unisonDetune = v;
    });
    registry_.registerParam("synth-unison-spread", 0.0f, 1.0f, [this](float v) {
        for (auto& l : multiSynth_.layers) l.cfg.unisonSpread = v;
    });
    registry_.registerParam("synth-phase", 0.0f, 1.0f, [this](float v) {
        for (auto& l : multiSynth_.layers) l.cfg.phaseOffset = v;
    });
    registry_.registerParam("synth-keytrack", 0.0f, 2.0f, [this](float v) {
        for (auto& l : multiSynth_.layers) l.cfg.keyTrack = v;
    });
    registry_.registerParam("synth-vel-cutoff", 0.0f, 1.0f, [this](float v) {
        for (auto& l : multiSynth_.layers) l.cfg.velToCutoff = v;
    });
    registry_.registerParam("synth-vel-amp", 0.0f, 2.0f, [this](float v) {
        for (auto& l : multiSynth_.layers) l.cfg.velToAmp = v;
    });
    registry_.registerParam("synth-filt-envamt", -1.0f, 1.0f, [this](float v) {
        for (auto& l : multiSynth_.layers) l.cfg.filterEnvAmt = v;
    });
    registry_.registerParam("synth-filt-atk", 0.001f, 5.0f, [this](float v) {
        for (auto& l : multiSynth_.layers) l.cfg.filtAttackSec = v;
    });
    registry_.registerParam("synth-filt-dec", 0.005f, 10.0f, [this](float v) {
        for (auto& l : multiSynth_.layers) l.cfg.filtDecaySec = v;
    });
    registry_.registerParam("synth-filt-sus", 0.0f, 1.0f, [this](float v) {
        for (auto& l : multiSynth_.layers) l.cfg.filtSustainLvl = v;
    });
    registry_.registerParam("synth-filt-rel", 0.005f, 10.0f, [this](float v) {
        for (auto& l : multiSynth_.layers) l.cfg.filtReleaseSec = v;
    });
    registry_.registerParam("synthLfoEnabled", 0.0f, 1.0f, [this](float v) { multiSynth_.lfos[0].enabled = (v >= 0.5f); });
    registry_.registerParam("synthLfoRate", 0.05f, 30.0f, [this](float v) { multiSynth_.lfos[0].rateHz = v; });
    registry_.registerParam("synthLfoDepth", 0.0f, 1.0f, [this](float v) { multiSynth_.lfos[0].depth = v; });
    registry_.registerParam("synthLfoCutoff", -5000.0f, 5000.0f, [this](float v) { multiSynth_.lfos[0].destCutoff = static_cast<int>(v); });
    registry_.registerParam("synthLfoPitch", 0.0f, 200.0f, [this](float v) { multiSynth_.lfos[0].destPitch = static_cast<int>(v); });
    registry_.registerParam("synthLfoWave", 0.0f, 3.0f, [this](float v) { multiSynth_.lfos[0].waveType = std::clamp(static_cast<int>(v), 0, 3); });

    registry_.registerParam("organModel", 0.0f, 3.0f, [this](float v) { organModule_.organModel = std::clamp(static_cast<int>(v), 0, 3); });
    registry_.registerParam("organVibratoMode", 0.0f, 6.0f, [this](float v) { organModule_.vibratoMode = std::clamp(static_cast<int>(v), 0, 6); });
    registry_.registerParam("organPercEnabled", 0.0f, 1.0f, [this](float v) { organModule_.percussionEnabled = (v >= 0.5f); });
    registry_.registerParam("organPercHarmonic", 2.0f, 3.0f, [this](float v) { organModule_.percussionHarmonic = (v >= 2.5f) ? 3 : 2; });
    registry_.registerParam("organPercDecaySec", 0.1f, 1.2f, [this](float v) { organModule_.percussionDecaySec = v; });
    registry_.registerParam("organPercLevel", 0.0f, 1.0f, [this](float v) { organModule_.percussionLevel = std::clamp(v, 0.0f, 1.0f); });
}

bool WorkstationHost::getSynthEnabled() const { return multiSynth_.synthEnabled; }
float WorkstationHost::getSynthMasterVol() const { return multiSynth_.synthMasterVol; }
float WorkstationHost::getSynthMacroCutoff() const { return multiSynth_.synthMacroCutoff; }

json WorkstationHost::getSynthLayersJson() const {
    json arr = json::array();
    for (int i = 0; i < 3; ++i) {
        const auto& c = multiSynth_.layers[i].cfg;
        arr.push_back({
            {"enabled", c.enabled}, {"waveType", c.waveType}, {"volume", c.volume},
            {"pan", c.pan}, {"octave", c.octave}, {"semitones", c.semitones},
            {"detuneCents", c.detuneCents}, {"cutoffHz", c.cutoffHz}, {"resonanceQ", c.resonanceQ},
            {"attackSec", c.attackSec}, {"decaySec", c.decaySec}, {"sustainLevel", c.sustainLevel},
            {"releaseSec", c.releaseSec}, {"unisonVoices", c.unisonVoices},
            {"unisonDetune", c.unisonDetune}, {"unisonSpread", c.unisonSpread},
            {"phaseOffset", c.phaseOffset}, {"keyTrack", c.keyTrack},
            {"velToCutoff", c.velToCutoff}, {"velToAmp", c.velToAmp},
            {"wavetableIdx", c.wavetableIdx}, {"samplePath", c.samplePath},
            {"filterType", c.filterType}, {"filterEnvAmt", c.filterEnvAmt},
            {"filterKeyTrack", c.filterKeyTrack},
            {"filtAttackSec", c.filtAttackSec}, {"filtDecaySec", c.filtDecaySec},
            {"filtSustainLvl", c.filtSustainLvl}, {"filtReleaseSec", c.filtReleaseSec},
            {"sawLevel", c.sawLevel}, {"pulseLevel", c.pulseLevel}, {"pulseWidth", c.pulseWidth}, {"pwmDepth", c.pwmDepth}, {"pwmRateHz", c.pwmRateHz}, {"subLevel", c.subLevel}, {"noiseLevel", c.noiseLevel}, {"osc2Level", c.osc2Level}, {"osc2Detune", c.osc2Detune}, {"osc2Semitones", c.osc2Semitones}, {"osc2Wave", c.osc2Wave}, {"highpassHz", c.highpassHz}, {"drive", c.drive}
        });
    }
    return arr;
}

json WorkstationHost::getSynthLFOsJson() const {
    json arr = json::array();
    for (int i = 0; i < SYNTH_NUM_LFOS; ++i) {
        const auto& l = multiSynth_.lfos[i];
        arr.push_back({
            {"enabled", l.enabled}, {"waveType", l.waveType}, {"rateHz", l.rateHz},
            {"depth", l.depth}, {"delaySec", l.delaySec}, {"fadeSec", l.fadeSec},
            {"destPitch", l.destPitch}, {"destCutoff", l.destCutoff},
            {"destVolume", l.destVolume}, {"destPan", l.destPan},
            {"destPhase", l.destPhase}
        });
    }
    return arr;
}

json WorkstationHost::getSynthEnvelopesJson() const {
    json arr = json::array();
    for (int i = 0; i < SYNTH_NUM_ENVS; ++i) {
        const auto& e = multiSynth_.envelopes[i];
        arr.push_back({
            {"delaySec", e.delaySec}, {"attackSec", e.attackSec}, {"holdSec", e.holdSec},
            {"decaySec", e.decaySec}, {"sustainLevel", e.sustainLevel}, {"releaseSec", e.releaseSec}
        });
    }
    return arr;
}

json WorkstationHost::getOrganParamsJson() const {
    const auto& o = organModule_;
    json drawbars = json::array();
    for (int i = 0; i < 9; ++i) drawbars.push_back(o.drawbars[i]);
    return {
        {"organEnabled", o.organEnabled}, {"organModel", o.organModel},
        {"organVolume", o.organVolume}, {"organRotaryFast", o.organRotaryFast},
        {"organDrive", o.organDrive}, {"drawbars", drawbars},
        {"vibratoMode", o.vibratoMode}, {"percussionEnabled", o.percussionEnabled},
        {"percussionHarmonic", o.percussionHarmonic},
        {"percussionDecaySec", o.percussionDecaySec}, {"percussionLevel", o.percussionLevel},
        {"useSamples", o.useSamples}, {"organSamplePath", o.organSamplePath}
    };
}

void WorkstationHost::applySynthPresetJson(const json& p) {
    multiSynth_.synthEnabled = p.value("synthEnabled", multiSynth_.synthEnabled);
    multiSynth_.synthMasterVol = p.value("synthMasterVol", multiSynth_.synthMasterVol);
    multiSynth_.synthMacroCutoff = p.value("synthMacroCutoff", multiSynth_.synthMacroCutoff);
    if (p.contains("layers") && p["layers"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(3, p["layers"].size()); ++i) {
            const auto& lp = p["layers"][i];
            auto& c = multiSynth_.layers[i].cfg;
            c.enabled = lp.value("enabled", c.enabled);
            c.waveType = lp.value("waveType", c.waveType);
            c.volume = lp.value("volume", c.volume);
            c.pan = lp.value("pan", c.pan);
            c.octave = lp.value("octave", c.octave);
            c.semitones = lp.value("semitones", c.semitones);
            c.detuneCents = lp.value("detuneCents", c.detuneCents);
            c.cutoffHz = lp.value("cutoffHz", c.cutoffHz);
            c.resonanceQ = lp.value("resonanceQ", c.resonanceQ);
            c.attackSec = lp.value("attackSec", c.attackSec);
            c.decaySec = lp.value("decaySec", c.decaySec);
            c.sustainLevel = lp.value("sustainLevel", c.sustainLevel);
            c.releaseSec = lp.value("releaseSec", c.releaseSec);
            c.unisonVoices = lp.value("unisonVoices", c.unisonVoices);
            c.unisonDetune = lp.value("unisonDetune", c.unisonDetune);
            c.unisonSpread = lp.value("unisonSpread", c.unisonSpread);
            c.phaseOffset = lp.value("phaseOffset", c.phaseOffset);
            c.keyTrack = lp.value("keyTrack", c.keyTrack);
            c.velToCutoff = lp.value("velToCutoff", c.velToCutoff);
            c.velToAmp = lp.value("velToAmp", c.velToAmp);
            c.wavetableIdx = lp.value("wavetableIdx", c.wavetableIdx);
            c.samplePath = lp.value("samplePath", c.samplePath);
            c.filterType = lp.value("filterType", c.filterType);
            c.filterEnvAmt = lp.value("filterEnvAmt", c.filterEnvAmt);
            c.filterKeyTrack = lp.value("filterKeyTrack", c.filterKeyTrack);
            c.filtAttackSec = lp.value("filtAttackSec", c.filtAttackSec);
            c.filtDecaySec = lp.value("filtDecaySec", c.filtDecaySec);
            c.filtSustainLvl = lp.value("filtSustainLvl", c.filtSustainLvl);
            c.filtReleaseSec = lp.value("filtReleaseSec", c.filtReleaseSec);
            c.sawLevel = lp.value("sawLevel", c.sawLevel);
            c.pulseLevel = lp.value("pulseLevel", c.pulseLevel);
            c.pulseWidth = lp.value("pulseWidth", c.pulseWidth);
            c.pwmDepth = lp.value("pwmDepth", c.pwmDepth);
            c.pwmRateHz = lp.value("pwmRateHz", c.pwmRateHz);
            c.subLevel = lp.value("subLevel", c.subLevel);
            c.noiseLevel = lp.value("noiseLevel", c.noiseLevel);
            c.osc2Level = lp.value("osc2Level", c.osc2Level);
            c.osc2Detune = lp.value("osc2Detune", c.osc2Detune);
            c.osc2Semitones = lp.value("osc2Semitones", c.osc2Semitones);
            c.osc2Wave = lp.value("osc2Wave", c.osc2Wave);
            c.highpassHz = lp.value("highpassHz", c.highpassHz);
            c.drive = lp.value("drive", c.drive);
        }
    }
    if (p.contains("lfos") && p["lfos"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(SYNTH_NUM_LFOS, p["lfos"].size()); ++i) {
            const auto& lp = p["lfos"][i];
            auto& l = multiSynth_.lfos[i];
            l.enabled = lp.value("enabled", l.enabled);
            l.waveType = lp.value("waveType", l.waveType);
            l.rateHz = lp.value("rateHz", l.rateHz);
            l.depth = lp.value("depth", l.depth);
            l.delaySec = lp.value("delaySec", l.delaySec);
            l.fadeSec = lp.value("fadeSec", l.fadeSec);
            l.destPitch = lp.value("destPitch", l.destPitch);
            l.destCutoff = lp.value("destCutoff", l.destCutoff);
            l.destVolume = lp.value("destVolume", l.destVolume);
            l.destPan = lp.value("destPan", l.destPan);
            l.destPhase = lp.value("destPhase", l.destPhase);
        }
    }
    if (p.contains("envelopes") && p["envelopes"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(SYNTH_NUM_ENVS, p["envelopes"].size()); ++i) {
            const auto& v = p["envelopes"][i];
            auto& e = multiSynth_.envelopes[i];
            e.delaySec = v.value("delaySec", e.delaySec);
            e.attackSec = v.value("attackSec", e.attackSec);
            e.holdSec = v.value("holdSec", e.holdSec);
            e.decaySec = v.value("decaySec", e.decaySec);
            e.sustainLevel = v.value("sustainLevel", e.sustainLevel);
            e.releaseSec = v.value("releaseSec", e.releaseSec);
        }
    }
}

void WorkstationHost::applyOrganPresetJson(const json& p) {
    organModule_.organEnabled = p.value("organEnabled", organModule_.organEnabled);
    organModule_.organModel = p.value("organModel", organModule_.organModel);
    organModule_.organVolume = p.value("organVolume", organModule_.organVolume);
    organModule_.organRotaryFast = p.value("organRotaryFast", organModule_.organRotaryFast);
    organModule_.organDrive = p.value("organDrive", organModule_.organDrive);
    organModule_.vibratoMode = p.value("vibratoMode", organModule_.vibratoMode);
    organModule_.percussionEnabled = p.value("percussionEnabled", organModule_.percussionEnabled);
    organModule_.percussionHarmonic = p.value("percussionHarmonic", organModule_.percussionHarmonic);
    organModule_.percussionDecaySec = p.value("percussionDecaySec", organModule_.percussionDecaySec);
    organModule_.percussionLevel = p.value("percussionLevel", organModule_.percussionLevel);
    organModule_.useSamples = p.value("useSamples", organModule_.useSamples);
    if (p.contains("drawbars") && p["drawbars"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(9, p["drawbars"].size()); ++i)
            organModule_.drawbars[i] = p["drawbars"][i].get<float>();
    }
}

bool WorkstationHost::loadOrganSample(const std::string& path) {
    return organModule_.loadOrganSample(path);
}

void WorkstationHost::addWavetable(const Wavetable& wt) {
    multiSynth_.addWavetable(wt);
}

const std::vector<Wavetable>& WorkstationHost::getWavetableBank() const {
    return multiSynth_.getWavetableBank();
}

bool WorkstationHost::applyParamById(const std::string& id, float val, bool normalized) {
    return registry_.setParamValue(id, val, normalized);
}

} // namespace NordDSP