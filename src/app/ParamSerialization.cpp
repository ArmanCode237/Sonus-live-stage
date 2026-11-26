#include "app/ParamSerialization.h"
#include "core/SynthLayerExtras.h"
#include "core/InsertFxJson.h"
#include <algorithm>

json serializeParams(const SynthPresetParams& p) {
    json eqArr = json::array();
    for (int i = 0; i < 5; ++i) {
        eqArr.push_back({
            {"freqHz", p.eqBands[i].freqHz},
            {"q",      p.eqBands[i].q},
            {"gainDb", p.eqBands[i].gainDb}
        });
    }

    json synthLayersArr = json::array();
    for (int i = 0; i < SYNTH_LAYER_COUNT; ++i) {
        const auto& c = p.synthLayers[i];
        synthLayersArr.push_back({
            {"enabled", c.enabled},
            {"waveType", c.waveType},{"synthesisType",c.synthesisType},
            {"dx7Voice",c.synthesisType==5?dx7ToHex(c.dx7Voice):std::string()},{"mt32Program",c.mt32Program},
            {"fmIndex",c.fmIndex},{"fmRatio",c.fmRatio},{"wavetableMorph",c.wavetableMorph},
            {"wheelMode",c.wheelMode},{"wheelCutoffOctaves",c.wheelCutoffOctaves},{"wheelAmpDb",c.wheelAmpDb},{"wheelVibratoCents",c.wheelVibratoCents},{"wheelVibratoRateHz",c.wheelVibratoRateHz},{"wheelWtMorph",c.wheelWtMorph},
            {"voiceMode",c.voiceMode},{"portamento",c.portamento},{"portamentoEnabled",c.portamentoEnabled},{"glideMode",c.glideMode},{"slide",c.slide},
            {"keyLow",c.keyLow},{"keyHigh",c.keyHigh},{"keyTranspose",c.keyTranspose},
            {"seqEnabled",c.seqEnabled},{"seqBpm",c.seqBpm},{"seqSteps",c.seqSteps},
            {"seqGate",c.seqGate},{"seqNotes",c.seqNotes},{"seqAccent",c.seqAccent},{"seqSlide",c.seqSlide},
            {"seqDivision",c.seqDivision},{"seqSync",c.seqSync},{"outGainDb",c.outGainDb},
            {"eqEnabled",c.eqEnabled},{"eqHighPass",c.eqHighPass},{"eqLowPass",c.eqLowPass},{"eqHighPassSlope",c.eqHighPassSlope},{"eqLowPassSlope",c.eqLowPassSlope},{"eqFreq",c.eqFreq},{"eqQ",c.eqQ},{"eqGain",c.eqGain},
            {"insertFx",insertFxToJson(c.insertFx)},
            {"volume", c.volume},
            {"pan", c.pan},
            {"octave", c.octave},
            {"semitones", c.semitones},
            {"detuneCents", c.detuneCents},
            {"cutoffHz", c.cutoffHz},
            {"resonanceQ", c.resonanceQ},
            {"attackSec", c.attackSec},
            {"decaySec", c.decaySec},
            {"sustainLevel", c.sustainLevel},
            {"releaseSec", c.releaseSec},
            {"unisonVoices", c.unisonVoices},
            {"unisonDetune", c.unisonDetune},
            {"unisonSpread", c.unisonSpread},
            {"phaseOffset", c.phaseOffset},
            {"keyTrack", c.keyTrack},
            {"velToCutoff", c.velToCutoff},
            {"velToAmp", c.velToAmp},
            {"wavetableIdx", c.wavetableIdx},
            {"samplePath", c.samplePath.str()},
            {"filterType", c.filterType},
            {"filterEnvAmt", c.filterEnvAmt},
            {"filterKeyTrack", c.filterKeyTrack},
            {"filtAttackSec", c.filtAttackSec},
            {"filtDecaySec", c.filtDecaySec},
            {"filtSustainLvl", c.filtSustainLvl},
            {"filtReleaseSec", c.filtReleaseSec},
            {"sawLevel", c.sawLevel}, {"pulseLevel", c.pulseLevel}, {"pulseWidth", c.pulseWidth}, {"pwmDepth", c.pwmDepth}, {"pwmRateHz", c.pwmRateHz}, {"subLevel", c.subLevel}, {"noiseLevel", c.noiseLevel}, {"osc2Level", c.osc2Level}, {"osc2Detune", c.osc2Detune}, {"osc2Semitones", c.osc2Semitones}, {"osc2Wave", c.osc2Wave}, {"highpassHz", c.highpassHz}, {"drive", c.drive}
        });
        synthLayerExtrasToJson(c, synthLayersArr.back());
    }

    json synthLfoArr = json::array();
    for (const auto& l : p.synthLfos) {
        synthLfoArr.push_back({
            {"enabled", l.enabled}, {"waveType", l.waveType},
            {"rateHz", l.rateHz}, {"depth", l.depth},
            {"delaySec", l.delaySec}, {"fadeSec", l.fadeSec},
            {"destPitch", l.destPitch}, {"destCutoff", l.destCutoff},
            {"destVolume", l.destVolume}, {"destPan", l.destPan},
            {"destPhase", l.destPhase}, {"keySync", l.keySync}
        });
    }
    json synthEnvArr = json::array();
    for (const auto& e : p.synthEnvelopes) {
        synthEnvArr.push_back({
            {"delaySec", e.delaySec}, {"attackSec", e.attackSec},
            {"holdSec", e.holdSec}, {"decaySec", e.decaySec},
            {"sustainLevel", e.sustainLevel}, {"releaseSec", e.releaseSec}
        });
    }

    json drawbarsArr = json::array();
    for (int d = 0; d < 9; ++d) {
        drawbarsArr.push_back(p.drawbars[d]);
    }

    const auto zoneJson=[](const KeyZone& z){return json{{"low",z.low},{"high",z.high},{"transpose",z.transpose},{"octave",z.octave}};};
    const auto fxJson=[](const ModuleFxConfig& c){return json{
        {"chorusEnabled",c.chorusEnabled},{"chorusRate",c.chorusRate},{"chorusDepth",c.chorusDepth},{"chorusMix",c.chorusMix},
        {"delayEnabled",c.delayEnabled},{"delayMs",c.delayMs},{"delayFeedback",c.delayFeedback},{"delayMix",c.delayMix},
        {"reverbEnabled",c.reverbEnabled},{"reverbType",c.reverbType},{"reverbMix",c.reverbMix},{"reverbSize",c.reverbSize},
        {"driveEnabled",c.driveEnabled},{"driveAmount",c.driveAmount},
        {"autoPanEnabled",c.autoPanEnabled},{"autoPanRate",c.autoPanRate},{"autoPanDepth",c.autoPanDepth},
        {"phaserEnabled",c.phaserEnabled},{"phaserRate",c.phaserRate},{"phaserDepth",c.phaserDepth},{"phaserFeedback",c.phaserFeedback},{"phaserMix",c.phaserMix},
        {"tremoloEnabled",c.tremoloEnabled},{"tremoloRate",c.tremoloRate},{"tremoloDepth",c.tremoloDepth},
        {"flangerEnabled",c.flangerEnabled},{"flangerRate",c.flangerRate},{"flangerDepth",c.flangerDepth},{"flangerFeedback",c.flangerFeedback},{"flangerMix",c.flangerMix}};};
    const auto studioJson=[](const StudioRackConfig& cfg){
        json slots=json::array(),sends=json::array(),eq=json::array();
        for(const auto& item:cfg.slots){
            json values=json::array();for(float v:item.p)values.push_back(v);
            slots.push_back({{"type",item.type},{"version",item.version},{"enabled",item.enabled},{"params",values}});
        }
        for(const auto& src:cfg.sends){json row=json::array();for(float v:src)row.push_back(v);sends.push_back(row);}
        for(float v:cfg.eqDb)eq.push_back(v);
        json freqs=json::array(),qs=json::array();
        for(float f:cfg.eqFreq)freqs.push_back(f);
        for(float q:cfg.eqQ)qs.push_back(q);
        return json{{"eqEnabled",cfg.eqEnabled},{"eqDb",eq},{"eqFreq",freqs},{"eqQ",qs},
                    {"eqHighPass",cfg.eqHighPass},{"eqLowPass",cfg.eqLowPass},
                    {"eqHighPassSlope",cfg.eqHighPassSlope},{"eqLowPassSlope",cfg.eqLowPassSlope},
                    {"eqInputDb",cfg.eqInputDb},{"eqOutputDb",cfg.eqOutputDb},
                    {"eqPosition",cfg.eqPosition},{"slots",slots},{"sends",sends}};
    };
    return {
        {"masterBpm",p.masterBpm},{"delayTempoSync",p.delayTempoSync},{"delayDivision",p.delayDivision},
        {"studioRoutingEnabled",p.studioRoutingEnabled},
        {"studioRacks",[&](){json racks=json::array();for(const auto& cfg:p.studioRacks)racks.push_back(studioJson(cfg));return racks;}()},
        {"pianoZone",zoneJson(p.pianoZone)},{"synthZone",zoneJson(p.synthZone)},
        {"organZone",zoneJson(p.organZone)},
        {"pianoLayerZones",[&](){json a=json::array();for(auto& z:p.pianoLayerZones)a.push_back(zoneJson(z));return a;}()},
        {"synthFx",fxJson(p.synthFx)},{"organFx",fxJson(p.organFx)},
        {"organFxSends",{{"modulation",p.organFxSends.modulation},{"delay",p.organFxSends.delay},{"reverb",p.organFxSends.reverb}}},
        {"flangerEnabled",p.flangerEnabled},{"flangerRate",p.flangerRate},{"flangerDepth",p.flangerDepth},
        {"flangerFeedback",p.flangerFeedback},{"flangerMix",p.flangerMix},
        {"synthFxSends",[&](){json a=json::array();for(auto& f:p.synthFxSends)
            a.push_back({{"modulation",f.modulation},{"delay",f.delay},{"reverb",f.reverb}});return a;}()},
        {"masterDriveEnabled",p.masterDriveEnabled},{"masterDriveAmount",p.masterDriveAmount},
        {"masterAutoPanEnabled",p.masterAutoPanEnabled},{"masterAutoPanRate",p.masterAutoPanRate},{"masterAutoPanDepth",p.masterAutoPanDepth},
        {"pianoEnabled", p.pianoEnabled},
        {"pianoVolume", p.pianoVolume},{"pianoPan",p.pianoPan},{"pianoTune",p.pianoTune},
        {"pianoLayers", [&] {
            json layers = json::array();
            for (int i = 0; i < PIANO_LAYER_COUNT; ++i)
                layers.push_back({{"enabled", p.pianoLayerEnabled[i]}, {"volume", p.pianoLayerVolume[i]}});
            return layers;
        }()},
        {"pianoSampleConfig", [&] {
            json configs = json::array();
            for (int i = 0; i < PIANO_LAYER_COUNT; ++i) {
                const auto& c = p.pianoLayerSample[i];
                configs.push_back({{"attackSec",c.attackSec},{"decaySec",c.decaySec},
                    {"sustainLevel",c.sustainLevel},{"releaseSec",c.releaseSec},
                    {"loopHold",c.loopHold},{"pan",c.pan},{"fineTuneCents",c.fineTuneCents},
                    {"cutoffHz",c.cutoffHz},{"resonanceQ",c.resonanceQ},{"gainDb",c.gainDb},
                    {"portamentoEnabled",c.portamentoEnabled},{"portamentoSec",c.portamentoSec},
                    {"portamentoMode",c.portamentoMode},{"wheelCutoffOctaves",c.wheelCutoffOctaves},{"wheelAmpDb",c.wheelAmpDb},
                    {"wheelCutoffEnabled",c.wheelCutoffEnabled},{"wheelCutoffInvert",c.wheelCutoffInvert},
                    {"wheelAmpEnabled",c.wheelAmpEnabled},{"wheelVibratoEnabled",c.wheelVibratoEnabled},
                    {"wheelVibratoCents",c.wheelVibratoCents},{"wheelVibratoRateHz",c.wheelVibratoRateHz},
                    {"monoMode",c.monoMode},{"legatoEnabled",c.legatoEnabled},
                    {"soundEqEnabled",c.soundEqEnabled},
                    {"soundEqFreq",c.soundEqFreq},{"soundEqQ",c.soundEqQ},{"soundEqGain",c.soundEqGain},
                    {"soundEqHighPass",c.soundEqHighPass},{"soundEqLowPass",c.soundEqLowPass},
                    {"soundEqHighPassSlope",c.soundEqHighPassSlope},{"soundEqLowPassSlope",c.soundEqLowPassSlope},
                    {"insertFx",insertFxToJson(c.insertFx)}});
            }
            return configs;
        }()},
        {"fxSends", [&] {
            json sends = json::array();
            for (int i = 0; i < FX_SOURCE_COUNT; ++i)
                sends.push_back({{"modulation", p.fxSends[i].modulation},
                                 {"delay", p.fxSends[i].delay}, {"reverb", p.fxSends[i].reverb}});
            return sends;
        }()},
        {"maxVoices", p.maxVoices},
        {"transposeSemis", p.transposeSemis},
        {"octaveShift", p.octaveShift},
        {"masterTuneCents", p.masterTuneCents},
        {"softRelease", p.softRelease},
        {"kbdTouch", p.kbdTouch},
        {"cutoffHz", p.cutoffHz},
        {"resonanceQ", p.resonanceQ},
        {"attackSec", p.attackSec},
        {"decaySec", p.decaySec},
        {"sustainLevel", p.sustainLevel},
        {"releaseSec", p.releaseSec},
        {"loopHold", p.loopHold},
        {"volume", p.volume},
        {"pan", p.pan},
        {"eqEnabled", p.eqEnabled},
        {"lowCutHz", p.lowCutHz},
        {"highCutHz", p.highCutHz},
        {"eqBands", eqArr},
        {"chorusEnabled", p.chorusEnabled},
        {"chorusRateHz", p.chorusRateHz},
        {"chorusDepth", p.chorusDepth},
        {"chorusMix", p.chorusMix},
        {"chorusDelay",p.chorusDelay},
        {"chorusTone",p.chorusTone},
        {"chorusFeedback",p.chorusFeedback},
        {"chorusWidth",p.chorusWidth},
        {"chorusVoices",p.chorusVoices},
        {"phaserEnabled", p.phaserEnabled},
        {"phaserRateHz", p.phaserRateHz},
        {"phaserDepth", p.phaserDepth},
        {"phaserFeedback", p.phaserFeedback},
        {"phaserMix", p.phaserMix},
        {"tremoloEnabled", p.tremoloEnabled},
        {"tremoloRateHz", p.tremoloRateHz},
        {"tremoloDepth", p.tremoloDepth},
        {"delayEnabled", p.delayEnabled},
        {"delayTimeMs", p.delayTimeMs},
        {"delayFeedback", p.delayFeedback},
        {"delayDamping", p.delayDamping},
        {"delayMix", p.delayMix},
        {"delayPingPong", p.delayPingPong},
        {"reverbEnabled", p.reverbEnabled},
        {"reverbType", p.reverbType},
        {"reverbMix", p.reverbMix},
        {"reverbSize", p.reverbSize},
        {"reverbDamping", p.reverbDamping},
        {"reverbPreDelayMs", p.reverbPreDelayMs},
        {"reverbShimmer", p.reverbShimmer},
        {"shimmerShiftSt", p.shimmerShiftSt},
        {"shimmerFeedback", p.shimmerFeedback},
        {"shimmerDiffusion", p.shimmerDiffusion},
        {"shimmerLowCutHz", p.shimmerLowCutHz},
        {"shimmerHighCutHz", p.shimmerHighCutHz},
        {"shimmerModRateHz", p.shimmerModRateHz},
        {"shimmerModDepth", p.shimmerModDepth},
        {"shimmerPitchMode", p.shimmerPitchMode},
        {"shimmerReverbMode", p.shimmerReverbMode},
        {"shimmerColorMode", p.shimmerColorMode},
    // Módulo 2: Synth 4-Layer
        {"synthEnabled", p.synthEnabled},
        {"synthPresetId", p.synthPresetId},
        {"synthMasterVol", p.synthMasterVol},{"synthPan",p.synthPan},{"synthTune",p.synthTune},
        {"synthMacroCutoff", p.synthMacroCutoff},{"synthWheelGlobalCutoff",p.synthWheelGlobalCutoff},{"synthWheelGlobalAmpDb",p.synthWheelGlobalAmpDb},{"synthOutGainDb",p.synthOutGainDb},
        {"synthVoiceMaster",p.synthVoiceMaster},{"synthVoiceMode",p.synthVoiceMode},
        {"synthSeqGlobalClock",p.synthSeqGlobalClock},{"synthSeqSharePattern",p.synthSeqSharePattern},
        {"synthMacros",p.synthMacros},{"modMatrix",[&]{json a=json::array();for(const auto& c:p.modMatrix)a.push_back({{"source",c.source},{"target",c.target},{"amount",c.amount},{"bipolar",c.bipolar},{"curve",c.curve}});return a;}()},{"synthPortamento",p.synthPortamento},
        {"synthPortamentoEnabled",p.synthPortamentoEnabled},
        {"synthGlideMode",p.synthGlideMode},{"synthSlide",p.synthSlide},
        {"synthLayers", synthLayersArr},
        {"synthLfos", synthLfoArr},
        {"synthEnvelopes", synthEnvArr},
        {"synthLfoEnabled", p.synthLfoEnabled},
        {"synthLfoWave", p.synthLfoWave},
        {"synthLfoRate", p.synthLfoRate},
        {"synthLfoDepth", p.synthLfoDepth},
        {"synthLfoPitch", p.synthLfoPitch},
        {"synthLfoCutoff", p.synthLfoCutoff},
        // Módulo 3: Órgano
        {"organEnabled", p.organEnabled},
        {"organModel", p.organModel},
        {"organVolume", p.organVolume},{"organPan",p.organPan},{"organTune",p.organTune},
        {"organRotaryFast", p.organRotaryFast},
        {"organDrive", p.organDrive},
        {"organRotaryMix", p.organRotaryMix},
        {"organRotaryFastRate",p.organRotaryFastRate},{"organRotaryRamp",p.organRotaryRamp},
        {"organRotaryStereoWidth",p.organRotaryStereoWidth},
        {"organVibratoDepth", p.organVibratoDepth},
        {"organVibratoRate", p.organVibratoRate},
        {"organChorusVoices",p.organChorusVoices},
        {"organChorusTone",p.organChorusTone},
        {"organChorusFeedback",p.organChorusFeedback},
        {"organChorusWidth",p.organChorusWidth},
        {"organChorusMix",p.organChorusMix},
        {"organChorusDepth",p.organChorusDepth},
        {"organChorusRate",p.organChorusRate},
        {"organChorusEnabled",p.organChorusEnabled},
        {"drawbars", drawbarsArr},
        {"organVibratoMode", p.organVibratoMode},
        {"organPercEnabled", p.organPercEnabled},
        {"organPercHarmonic", p.organPercHarmonic},
        {"organPercDecaySec", p.organPercDecaySec},
        {"organPercLevel", p.organPercLevel}
    };
}

SynthPresetParams deserializeParams(const json& j, const SynthPresetParams& fallback) {
    SynthPresetParams p = fallback;
    if (!j.is_object()) return p;
    // Old programs preserve their previous DSP signature and FX routing.
    // SONUS global tempo and legacy delay transport are persisted with each performance.
    p.masterBpm=std::clamp(j.value("masterBpm",p.masterBpm),30.f,300.f);
    p.delayTempoSync=j.value("delayTempoSync",p.delayTempoSync);
    p.delayDivision=std::clamp(j.value("delayDivision",p.delayDivision),0,15);
    // New programs explicitly store studioRoutingEnabled and studioRacks.
    if(j.contains("studioRacks")&&j["studioRacks"].is_array()){
        p.studioRoutingEnabled=j.value("studioRoutingEnabled",true);
        for(size_t rack=0;rack<std::min<size_t>(4,j["studioRacks"].size());++rack){
            const auto& obj=j["studioRacks"][rack];if(!obj.is_object())continue;
            auto& c=p.studioRacks[rack];
            if(obj.contains("eqEnabled")&&obj["eqEnabled"].is_boolean())c.eqEnabled=obj["eqEnabled"].get<bool>();
            if(obj.contains("eqDb")&&obj["eqDb"].is_array())
                for(size_t i=0;i<std::min<size_t>(5,obj["eqDb"].size());++i)
                    if(obj["eqDb"][i].is_number())c.eqDb[i]=std::clamp(obj["eqDb"][i].get<float>(),-15.f,15.f);
            if(obj.contains("eqFreq")&&obj["eqFreq"].is_array())
                for(size_t i=0;i<std::min<size_t>(5,obj["eqFreq"].size());++i)
                    if(obj["eqFreq"][i].is_number())c.eqFreq[i]=std::clamp(obj["eqFreq"][i].get<float>(),30.f,16000.f);
            if(obj.contains("eqQ")&&obj["eqQ"].is_array())
                for(size_t i=0;i<std::min<size_t>(5,obj["eqQ"].size());++i)
                    if(obj["eqQ"][i].is_number())c.eqQ[i]=std::clamp(obj["eqQ"][i].get<float>(),.3f,12.f);
            c.eqHighPass=std::clamp(obj.value("eqHighPass",c.eqHighPass),20.f,1000.f);
            c.eqLowPass=std::clamp(obj.value("eqLowPass",c.eqLowPass),1000.f,20000.f);
            c.eqHighPassSlope=SonusDSP::normalizeEqSlope(obj.value("eqHighPassSlope",12));
            c.eqLowPassSlope=SonusDSP::normalizeEqSlope(obj.value("eqLowPassSlope",12));
            c.eqInputDb=std::clamp(obj.value("eqInputDb",c.eqInputDb),-24.f,24.f);
            c.eqOutputDb=std::clamp(obj.value("eqOutputDb",c.eqOutputDb),-24.f,24.f);
            c.eqPosition=std::clamp(obj.value("eqPosition",c.eqPosition),0,5);
            if(obj.contains("slots")&&obj["slots"].is_array()){
                for(size_t k=0;k<std::min<size_t>(5,obj["slots"].size());++k){
                    const auto& slot=obj["slots"][k];if(!slot.is_object())continue;
                    auto& dst=c.slots[k];
                    if(slot.contains("type")&&slot["type"].is_number_integer())dst.type=std::clamp(slot["type"].get<int>(),0,12);
                    dst.version=std::clamp(slot.value("version",1),1,STUDIO_SLOT_MAX_VERSION);
                    if(slot.contains("enabled")&&slot["enabled"].is_boolean())dst.enabled=slot["enabled"].get<bool>();
                    if(slot.contains("params")&&slot["params"].is_array())
                        for(size_t i=0;i<std::min<size_t>(STUDIO_PARAM_COUNT,slot["params"].size());++i)
                            if(slot["params"][i].is_number())dst.p[i]=std::clamp(slot["params"][i].get<float>(),0.f,1.f);
                    // v35: a shimmer saved by the v1–v4 engines is removed (empty slot): its
                    // settings do not translate to Atmosphere v5, so the user sets it up again.
                    if(isShimmerSlot(dst)&&dst.version<SHIMMER_ENGINE_VERSION)dst=StudioSlotConfig{};
                }
            }
            if(obj.contains("sends")&&obj["sends"].is_array())
                for(size_t src=0;src<std::min<size_t>(5,obj["sends"].size());++src){
                    const auto& row=obj["sends"][src];if(!row.is_array())continue;
                    for(size_t i=0;i<std::min<size_t>(5,row.size());++i)
                        if(row[i].is_number())c.sends[src][i]=std::clamp(row[i].get<float>(),0.f,1.f);
                }
        }
    }else p.studioRoutingEnabled=true;
    // Persistent keyzones and independent effect racks; old programs use defaults.
    const auto loadZone=[](const json& jz,KeyZone& z){if(!jz.is_object())return;
        z.low=std::clamp(jz.value("low",z.low),0,127);z.high=std::clamp(jz.value("high",z.high),0,127);
        z.transpose=std::clamp(jz.value("transpose",z.transpose),-24,24);
        z.octave=std::clamp(jz.value("octave",z.octave),-2,2);};
    if(j.contains("pianoZone"))loadZone(j["pianoZone"],p.pianoZone);
    if(j.contains("synthZone"))loadZone(j["synthZone"],p.synthZone);
    if(j.contains("organZone"))loadZone(j["organZone"],p.organZone);
    if(j.contains("pianoLayerZones")&&j["pianoLayerZones"].is_array())
        for(size_t i=0;i<std::min<size_t>(PIANO_LAYER_COUNT,j["pianoLayerZones"].size());i++)loadZone(j["pianoLayerZones"][i],p.pianoLayerZones[i]);
    const auto loadFx=[](const json& f,ModuleFxConfig& c){if(!f.is_object())return;
        c.chorusEnabled=f.value("chorusEnabled",c.chorusEnabled);c.chorusRate=f.value("chorusRate",c.chorusRate);
        c.chorusDepth=f.value("chorusDepth",c.chorusDepth);c.chorusMix=f.value("chorusMix",c.chorusMix);
        c.delayEnabled=f.value("delayEnabled",c.delayEnabled);c.delayMs=f.value("delayMs",c.delayMs);
        c.delayFeedback=f.value("delayFeedback",c.delayFeedback);c.delayMix=f.value("delayMix",c.delayMix);
        c.reverbEnabled=f.value("reverbEnabled",c.reverbEnabled);c.reverbType=f.value("reverbType",c.reverbType);
        c.reverbMix=f.value("reverbMix",c.reverbMix);c.reverbSize=f.value("reverbSize",c.reverbSize);
        c.driveEnabled=f.value("driveEnabled",c.driveEnabled);c.driveAmount=f.value("driveAmount",c.driveAmount);
        c.autoPanEnabled=f.value("autoPanEnabled",c.autoPanEnabled);
        c.autoPanRate=f.value("autoPanRate",c.autoPanRate);c.autoPanDepth=f.value("autoPanDepth",c.autoPanDepth);
        c.phaserEnabled=f.value("phaserEnabled",c.phaserEnabled);c.phaserRate=f.value("phaserRate",c.phaserRate);
        c.phaserDepth=f.value("phaserDepth",c.phaserDepth);c.phaserFeedback=f.value("phaserFeedback",c.phaserFeedback);c.phaserMix=f.value("phaserMix",c.phaserMix);
        c.tremoloEnabled=f.value("tremoloEnabled",c.tremoloEnabled);c.tremoloRate=f.value("tremoloRate",c.tremoloRate);c.tremoloDepth=f.value("tremoloDepth",c.tremoloDepth);
        c.flangerEnabled=f.value("flangerEnabled",c.flangerEnabled);c.flangerRate=f.value("flangerRate",c.flangerRate);c.flangerDepth=f.value("flangerDepth",c.flangerDepth);
        c.flangerFeedback=f.value("flangerFeedback",c.flangerFeedback);c.flangerMix=f.value("flangerMix",c.flangerMix);};
    if(j.contains("synthFx"))loadFx(j["synthFx"],p.synthFx);
    if(j.contains("organFx"))loadFx(j["organFx"],p.organFx);
    if(j.contains("organFxSends")&&j["organFxSends"].is_object()){
        const auto& f=j["organFxSends"];
        p.organFxSends.modulation=std::clamp(f.value("modulation",p.organFxSends.modulation),0.f,1.f);
        p.organFxSends.delay=std::clamp(f.value("delay",p.organFxSends.delay),0.f,1.f);
        p.organFxSends.reverb=std::clamp(f.value("reverb",p.organFxSends.reverb),0.f,1.f);
    }
    if(j.contains("synthFxSends")&&j["synthFxSends"].is_array())
        for(size_t i=0;i<std::min<size_t>(3,j["synthFxSends"].size());i++){
            const auto& f=j["synthFxSends"][i];if(!f.is_object())continue;
            p.synthFxSends[i].modulation=std::clamp(f.value("modulation",p.synthFxSends[i].modulation),0.f,1.f);
            p.synthFxSends[i].delay=std::clamp(f.value("delay",p.synthFxSends[i].delay),0.f,1.f);
            p.synthFxSends[i].reverb=std::clamp(f.value("reverb",p.synthFxSends[i].reverb),0.f,1.f);
        }
    p.flangerEnabled=j.value("flangerEnabled",p.flangerEnabled);
    p.flangerRate=j.value("flangerRate",p.flangerRate);
    p.flangerDepth=j.value("flangerDepth",p.flangerDepth);
    p.flangerFeedback=j.value("flangerFeedback",p.flangerFeedback);
    p.flangerMix=j.value("flangerMix",p.flangerMix);
    p.masterDriveEnabled=j.value("masterDriveEnabled",p.masterDriveEnabled);
    p.masterDriveAmount=j.value("masterDriveAmount",p.masterDriveAmount);
    p.masterAutoPanEnabled=j.value("masterAutoPanEnabled",p.masterAutoPanEnabled);
    p.masterAutoPanRate=j.value("masterAutoPanRate",p.masterAutoPanRate);
    p.masterAutoPanDepth=j.value("masterAutoPanDepth",p.masterAutoPanDepth);

    p.pianoEnabled     = j.value("pianoEnabled", p.pianoEnabled);
    p.pianoVolume      = j.value("pianoVolume", p.pianoVolume);
    p.pianoPan=j.value("pianoPan",p.pianoPan);p.pianoTune=j.value("pianoTune",p.pianoTune);
    p.synthPan=j.value("synthPan",p.synthPan);p.synthTune=j.value("synthTune",p.synthTune);
    p.organPan=j.value("organPan",p.organPan);p.organTune=j.value("organTune",p.organTune);
    if (j.contains("pianoLayers") && j["pianoLayers"].is_array()) {
        // Legacy programs had 3 layers; don't leak enabled L4-L5 from current patch.
        for (size_t i = j["pianoLayers"].size(); i < PIANO_LAYER_COUNT; ++i)
            p.pianoLayerEnabled[i] = false;
        for (size_t i = 0; i < std::min<size_t>(PIANO_LAYER_COUNT, j["pianoLayers"].size()); ++i) {
            const auto& layer = j["pianoLayers"][i];
            if (!layer.is_object()) continue;
            p.pianoLayerEnabled[i] = layer.value("enabled", p.pianoLayerEnabled[i]);
            p.pianoLayerVolume[i] = layer.value("volume", p.pianoLayerVolume[i]);
        }
    }
    if (j.contains("fxSends") && j["fxSends"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(FX_SOURCE_COUNT, j["fxSends"].size()); ++i) {
            if (!j["fxSends"][i].is_object()) continue;
            const auto& f = j["fxSends"][i];
            p.fxSends[i].modulation = std::clamp(f.value("modulation", p.fxSends[i].modulation), 0.0f, 1.0f);
            p.fxSends[i].delay = std::clamp(f.value("delay", p.fxSends[i].delay), 0.0f, 1.0f);
            p.fxSends[i].reverb = std::clamp(f.value("reverb", p.fxSends[i].reverb), 0.0f, 1.0f);
        }
    }
    p.maxVoices        = j.value("maxVoices", p.maxVoices);
    p.transposeSemis   = j.value("transposeSemis", p.transposeSemis);
    p.octaveShift      = j.value("octaveShift", p.octaveShift);
    p.masterTuneCents  = j.value("masterTuneCents", p.masterTuneCents);
    p.softRelease      = j.value("softRelease", p.softRelease);
    p.kbdTouch         = j.value("kbdTouch", p.kbdTouch);
    p.cutoffHz         = j.value("cutoffHz", p.cutoffHz);
    p.resonanceQ       = j.value("resonanceQ", p.resonanceQ);
    p.attackSec        = j.value("attackSec", p.attackSec);
    p.decaySec         = j.value("decaySec", p.decaySec);
    p.sustainLevel     = j.value("sustainLevel", p.sustainLevel);
    p.releaseSec       = j.value("releaseSec", p.releaseSec);
    p.loopHold         = j.value("loopHold", p.loopHold);
    // Backwards compatible: old programs without per-layer envelopes use the old global sample envelope.
    if (j.contains("pianoSampleConfig") && j["pianoSampleConfig"].is_array()) {
        for (size_t i=0;i<std::min<size_t>(PIANO_LAYER_COUNT,j["pianoSampleConfig"].size());++i) {
            const auto& c=j["pianoSampleConfig"][i];
            if (!c.is_object()) continue;
            auto& dst=p.pianoLayerSample[i];
            dst.gainDb=std::clamp(c.value("gainDb",dst.gainDb),-36.f,18.f);
            dst.attackSec=c.value("attackSec",dst.attackSec);
            dst.decaySec=c.value("decaySec",dst.decaySec);
            dst.sustainLevel=c.value("sustainLevel",dst.sustainLevel);
            dst.portamentoEnabled=c.value("portamentoEnabled",dst.portamentoEnabled);
            dst.portamentoSec=std::clamp(c.value("portamentoSec",dst.portamentoSec),0.f,2.f);
            dst.portamentoMode=std::clamp(c.value("portamentoMode",dst.portamentoMode),0,1);
            // A Sound / Performance is self-contained: no inserts stored = inserts off.
            dst.insertFx=InsertFxChain{};
            insertFxFromJson(c.value("insertFx",json::array()),dst.insertFx);
            dst.wheelCutoffOctaves=std::clamp(c.value("wheelCutoffOctaves",dst.wheelCutoffOctaves),-6.f,6.f);
            dst.wheelAmpDb=std::clamp(c.value("wheelAmpDb",dst.wheelAmpDb),-24.f,12.f);
            dst.wheelCutoffEnabled=c.value("wheelCutoffEnabled",std::abs(dst.wheelCutoffOctaves)>0.0001f);
            dst.wheelCutoffInvert=c.value("wheelCutoffInvert",false);
            dst.wheelAmpEnabled=c.value("wheelAmpEnabled",std::abs(dst.wheelAmpDb)>0.0001f);
            dst.wheelVibratoEnabled=c.value("wheelVibratoEnabled",false);
            dst.wheelVibratoCents=std::clamp(c.value("wheelVibratoCents",dst.wheelVibratoCents),0.f,200.f);
            dst.wheelVibratoRateHz=std::clamp(c.value("wheelVibratoRateHz",dst.wheelVibratoRateHz),.1f,12.f);
            dst.monoMode=c.value("monoMode",false);
            dst.legatoEnabled=c.value("legatoEnabled",false);
            dst.releaseSec=c.value("releaseSec",dst.releaseSec);
            dst.loopHold=c.value("loopHold",dst.loopHold);
            dst.pan=c.value("pan",dst.pan);
            dst.fineTuneCents=c.value("fineTuneCents",dst.fineTuneCents);
            dst.cutoffHz=std::clamp(c.value("cutoffHz",dst.cutoffHz),40.f,20000.f);
            dst.resonanceQ=std::clamp(c.value("resonanceQ",dst.resonanceQ),.5f,12.f);
            dst.soundEqEnabled=c.value("soundEqEnabled",false);
            dst.soundEqHighPass=std::clamp(c.value("soundEqHighPass",20.f),20.f,1000.f);
            dst.soundEqLowPass=std::clamp(c.value("soundEqLowPass",20000.f),1000.f,20000.f);
            dst.soundEqHighPassSlope=SonusDSP::normalizeEqSlope(c.value("soundEqHighPassSlope",12));
            dst.soundEqLowPassSlope=SonusDSP::normalizeEqSlope(c.value("soundEqLowPassSlope",12));
            const auto decode3=[&](const char* key,std::array<float,3>& dstv,float lo,float hi){
                if(c.contains(key)&&c[key].is_array())for(size_t b=0;b<std::min<size_t>(3,c[key].size());++b)
                    if(c[key][b].is_number())dstv[b]=std::clamp(c[key][b].get<float>(),lo,hi);
            };
            decode3("soundEqFreq",dst.soundEqFreq,30.f,16000.f);
            decode3("soundEqQ",dst.soundEqQ,.3f,12.f);
            decode3("soundEqGain",dst.soundEqGain,-15.f,15.f);
        }
    } else {
        for (auto& c : p.pianoLayerSample) {
            c.attackSec=std::max(0.005f,p.attackSec);
            c.decaySec=p.decaySec;
            c.sustainLevel=p.sustainLevel;
            c.releaseSec=p.releaseSec;
            c.loopHold=p.loopHold;
            c.pan=0.f;
            c.fineTuneCents=0.f;
        }
    }
    p.volume           = j.value("volume", p.volume);
    p.pan              = j.value("pan", p.pan);

    p.eqEnabled        = j.value("eqEnabled", p.eqEnabled);
    p.lowCutHz         = j.value("lowCutHz", p.lowCutHz);
    p.highCutHz        = j.value("highCutHz", p.highCutHz);
    if (j.contains("eqBands") && j["eqBands"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(5, j["eqBands"].size()); ++i) {
            p.eqBands[i].freqHz = j["eqBands"][i].value("freqHz", p.eqBands[i].freqHz);
            p.eqBands[i].q      = j["eqBands"][i].value("q", p.eqBands[i].q);
            p.eqBands[i].gainDb = j["eqBands"][i].value("gainDb", p.eqBands[i].gainDb);
        }
    }

    p.chorusEnabled    = j.value("chorusEnabled", p.chorusEnabled);
    p.chorusRateHz     = j.value("chorusRateHz", p.chorusRateHz);
    p.chorusDepth      = j.value("chorusDepth", p.chorusDepth);
    p.chorusMix        = j.value("chorusMix", p.chorusMix);
    p.chorusDelay = j.value("chorusDelay", p.chorusDelay);
    p.chorusTone = j.value("chorusTone", p.chorusTone);
    p.chorusFeedback = j.value("chorusFeedback", p.chorusFeedback);
    p.chorusWidth = j.value("chorusWidth", p.chorusWidth);
    p.chorusVoices = j.value("chorusVoices", p.chorusVoices);

    p.phaserEnabled    = j.value("phaserEnabled", p.phaserEnabled);
    p.phaserRateHz     = j.value("phaserRateHz", p.phaserRateHz);
    p.phaserDepth      = j.value("phaserDepth", p.phaserDepth);
    p.phaserFeedback   = j.value("phaserFeedback", p.phaserFeedback);
    p.phaserMix        = j.value("phaserMix", p.phaserMix);

    p.tremoloEnabled   = j.value("tremoloEnabled", p.tremoloEnabled);
    p.tremoloRateHz    = j.value("tremoloRateHz", p.tremoloRateHz);
    p.tremoloDepth     = j.value("tremoloDepth", p.tremoloDepth);

    p.delayEnabled     = j.value("delayEnabled", p.delayEnabled);
    p.delayTimeMs      = j.value("delayTimeMs", p.delayTimeMs);
    p.delayFeedback    = j.value("delayFeedback", p.delayFeedback);
    p.delayDamping     = j.value("delayDamping", p.delayDamping);
    p.delayMix         = j.value("delayMix", p.delayMix);
    p.delayPingPong    = j.value("delayPingPong", p.delayPingPong);

    p.reverbEnabled    = j.value("reverbEnabled", p.reverbEnabled);
    p.reverbType       = j.value("reverbType", p.reverbType);
    p.reverbMix        = j.value("reverbMix", p.reverbMix);
    p.reverbSize       = j.value("reverbSize", p.reverbSize);
    p.reverbDamping    = j.value("reverbDamping", p.reverbDamping);
    p.reverbPreDelayMs = j.value("reverbPreDelayMs", p.reverbPreDelayMs);
    p.reverbShimmer    = j.value("reverbShimmer", p.reverbShimmer);

    p.shimmerShiftSt    = j.value("shimmerShiftSt", p.shimmerShiftSt);
    p.shimmerFeedback   = j.value("shimmerFeedback", p.shimmerFeedback);
    p.shimmerDiffusion  = j.value("shimmerDiffusion", p.shimmerDiffusion);
    p.shimmerLowCutHz   = j.value("shimmerLowCutHz", p.shimmerLowCutHz);
    p.shimmerHighCutHz  = j.value("shimmerHighCutHz", p.shimmerHighCutHz);
    p.shimmerModRateHz  = j.value("shimmerModRateHz", p.shimmerModRateHz);
    p.shimmerModDepth   = j.value("shimmerModDepth", p.shimmerModDepth);
    p.shimmerPitchMode  = j.value("shimmerPitchMode", p.shimmerPitchMode);
    p.shimmerReverbMode = j.value("shimmerReverbMode", p.shimmerReverbMode);
    p.shimmerColorMode  = j.value("shimmerColorMode", p.shimmerColorMode);

    // Módulo 2: Synth 4-Layer
    p.synthEnabled      = j.value("synthEnabled", p.synthEnabled);
    p.synthPresetId     = j.value("synthPresetId", p.synthPresetId);
    p.synthMasterVol    = j.value("synthMasterVol", p.synthMasterVol);
    p.synthMacroCutoff  = j.value("synthMacroCutoff", p.synthMacroCutoff);
    p.synthWheelGlobalCutoff=std::clamp(j.value("synthWheelGlobalCutoff",p.synthWheelGlobalCutoff),-6.f,6.f);
    p.synthWheelGlobalAmpDb=std::clamp(j.value("synthWheelGlobalAmpDb",p.synthWheelGlobalAmpDb),-24.f,12.f);
    p.synthOutGainDb=std::clamp(j.value("synthOutGainDb",p.synthOutGainDb),-24.f,24.f);
    p.synthVoiceMaster=j.value("synthVoiceMaster",p.synthVoiceMaster);
    p.synthVoiceMode = j.value("synthVoiceMode", p.synthVoiceMode);
    p.synthSeqGlobalClock = j.value("synthSeqGlobalClock", p.synthSeqGlobalClock);
    p.synthSeqSharePattern = j.value("synthSeqSharePattern", p.synthSeqSharePattern);
    if (j.contains("synthMacros") && j["synthMacros"].is_array())
        for (size_t i = 0; i < std::min<size_t>(4, j["synthMacros"].size()); ++i) p.synthMacros[i] = std::clamp(j["synthMacros"][i].get<float>(), 0.f, 1.f);
    p.modMatrix.clear();
    if (j.contains("modMatrix") && j["modMatrix"].is_array())
        for (const auto& c : j["modMatrix"]) if (c.is_object())
            p.modMatrix.push_back({c.value("source",0), c.value("target",std::string()), c.value("amount",.25f), c.value("bipolar",false), c.value("curve",0)});
    p.synthPortamento = j.value("synthPortamento",p.synthPortamento);
    p.synthPortamentoEnabled = j.value("synthPortamentoEnabled",p.synthPortamento>0.001f);
    p.synthGlideMode = j.value("synthGlideMode",p.synthGlideMode);
    p.synthSlide = j.value("synthSlide",p.synthSlide);
    p.synthLfoEnabled = j.value("synthLfoEnabled", p.synthLfoEnabled);
    p.synthLfoWave = j.value("synthLfoWave", p.synthLfoWave);
    p.synthLfoRate = j.value("synthLfoRate", p.synthLfoRate);
    p.synthLfoDepth = j.value("synthLfoDepth", p.synthLfoDepth);
    p.synthLfoPitch = j.value("synthLfoPitch", p.synthLfoPitch);
    p.synthLfoCutoff = j.value("synthLfoCutoff", p.synthLfoCutoff);
    if (j.contains("synthLayers") && j["synthLayers"].is_array()) {
        for(size_t i=j["synthLayers"].size();i<SYNTH_LAYER_COUNT;++i)p.synthLayers[i].enabled=false;
        for (size_t i = 0; i < std::min<size_t>(SYNTH_LAYER_COUNT, j["synthLayers"].size()); ++i) {
            const auto& lp = j["synthLayers"][i];
            auto& c = p.synthLayers[i];
            insertFxFromJson(lp.value("insertFx",json::array()),c.insertFx);
            c.synthesisType=std::clamp(lp.value("synthesisType",c.synthesisType),0,SYNTH_SYNTHESIS_TYPE_COUNT-1);
            if(lp.contains("dx7Voice") && lp["dx7Voice"].is_string() && dx7FromHex(lp["dx7Voice"].get<std::string>(),c.dx7Voice))
                c.dx7Revision=nextDx7Revision();
            c.mt32Program=std::clamp(lp.value("mt32Program",c.mt32Program),0,127);
            c.fmIndex=lp.value("fmIndex",c.fmIndex);c.fmRatio=lp.value("fmRatio",c.fmRatio);
            c.wavetableMorph=lp.value("wavetableMorph",c.wavetableMorph);
            // Legacy patches with custom CC1 routes retain their original routing.
            c.wheelMode=std::clamp(lp.value("wheelMode",2),0,3);
            c.wheelCutoffOctaves=std::clamp(lp.value("wheelCutoffOctaves",c.wheelCutoffOctaves),-6.f,6.f);
            c.wheelAmpDb=std::clamp(lp.value("wheelAmpDb",c.wheelAmpDb),-24.f,12.f);
            c.wheelVibratoCents=std::clamp(lp.value("wheelVibratoCents",c.wheelVibratoCents),0.f,200.f);
            c.wheelVibratoRateHz=std::clamp(lp.value("wheelVibratoRateHz",c.wheelVibratoRateHz),.1f,12.f);
            c.wheelWtMorph=std::clamp(lp.value("wheelWtMorph",c.wheelWtMorph),-1.f,1.f);
            c.voiceMode=std::clamp(lp.value("voiceMode",c.voiceMode),0,2);
            c.portamento=lp.value("portamento",c.portamento);
            c.portamentoEnabled=lp.value("portamentoEnabled",c.portamento>0.001f);
            c.glideMode=lp.value("glideMode",c.glideMode);
            c.slide=lp.value("slide",c.slide);
            c.keyLow=std::clamp(lp.value("keyLow",c.keyLow),0,127);
            c.keyHigh=std::clamp(lp.value("keyHigh",c.keyHigh),0,127);
            c.keyTranspose=std::clamp(lp.value("keyTranspose",c.keyTranspose),-24,24);
            c.seqEnabled=lp.value("seqEnabled",c.seqEnabled);
            c.seqBpm=std::clamp(lp.value("seqBpm",c.seqBpm),30.f,300.f);
            c.seqSteps=std::clamp(lp.value("seqSteps",c.seqSteps),1,16);
            c.seqGate=std::clamp(lp.value("seqGate",c.seqGate),.05f,1.f);
            c.seqDivision=std::clamp(lp.value("seqDivision",c.seqDivision),0,5);
            c.seqSync=lp.value("seqSync",c.seqSync);
            c.outGainDb=std::clamp(lp.value("outGainDb",c.outGainDb),-24.f,24.f);
            c.eqEnabled=lp.value("eqEnabled",c.eqEnabled);
            c.eqHighPass=std::clamp(lp.value("eqHighPass",c.eqHighPass),20.f,1000.f);
            c.eqLowPass=std::clamp(lp.value("eqLowPass",c.eqLowPass),1000.f,20000.f);
            c.eqHighPassSlope=SonusDSP::normalizeEqSlope(lp.value("eqHighPassSlope",c.eqHighPassSlope));
            c.eqLowPassSlope=SonusDSP::normalizeEqSlope(lp.value("eqLowPassSlope",c.eqLowPassSlope));
            for(int b=0;b<3;++b){
                if(lp.contains("eqFreq")&&lp["eqFreq"].is_array()&&b<int(lp["eqFreq"].size()))c.eqFreq[size_t(b)]=std::clamp(lp["eqFreq"][size_t(b)].get<float>(),30.f,16000.f);
                if(lp.contains("eqQ")&&lp["eqQ"].is_array()&&b<int(lp["eqQ"].size()))c.eqQ[size_t(b)]=std::clamp(lp["eqQ"][size_t(b)].get<float>(),.3f,12.f);
                if(lp.contains("eqGain")&&lp["eqGain"].is_array()&&b<int(lp["eqGain"].size()))c.eqGain[size_t(b)]=std::clamp(lp["eqGain"][size_t(b)].get<float>(),-15.f,15.f);
            }
            for(const char* key:{"seqAccent","seqSlide"})
                if(lp.contains(key)&&lp[key].is_array())
                    for(size_t k=0;k<std::min<size_t>(16,lp[key].size());++k)
                        (std::string(key)=="seqAccent"?c.seqAccent:c.seqSlide)[k]=lp[key][k].is_number()&&lp[key][k].get<int>()!=0?1:0;
            if(lp.contains("seqNotes")&&lp["seqNotes"].is_array())
                for(size_t k=0;k<std::min<size_t>(16,lp["seqNotes"].size());++k)
                    if(lp["seqNotes"][k].is_number_integer()){
                        const int note=lp["seqNotes"][k].get<int>();c.seqNotes[k]=note==99?99:std::clamp(note,-24,24);
                    }
            c.enabled        = lp.value("enabled", c.enabled);
            c.waveType       = lp.value("waveType", c.waveType);
            c.volume         = lp.value("volume", c.volume);
            c.pan            = lp.value("pan", c.pan);
            c.octave         = lp.value("octave", c.octave);
            c.semitones      = lp.value("semitones", c.semitones);
            c.detuneCents    = lp.value("detuneCents", c.detuneCents);
            c.cutoffHz       = lp.value("cutoffHz", c.cutoffHz);
            c.resonanceQ     = lp.value("resonanceQ", c.resonanceQ);
            c.attackSec      = lp.value("attackSec", c.attackSec);
            c.decaySec       = lp.value("decaySec", c.decaySec);
            c.sustainLevel   = lp.value("sustainLevel", c.sustainLevel);
            c.releaseSec     = lp.value("releaseSec", c.releaseSec);
            c.unisonVoices   = lp.value("unisonVoices", c.unisonVoices);
            c.unisonDetune   = lp.value("unisonDetune", c.unisonDetune);
            c.unisonSpread   = lp.value("unisonSpread", c.unisonSpread);
            c.phaseOffset    = lp.value("phaseOffset", c.phaseOffset);
            c.keyTrack       = lp.value("keyTrack", c.keyTrack);
            c.velToCutoff    = lp.value("velToCutoff", c.velToCutoff);
            c.velToAmp       = lp.value("velToAmp", c.velToAmp);
            c.wavetableIdx   = lp.value("wavetableIdx", c.wavetableIdx);
            c.samplePath     = lp.value("samplePath", c.samplePath.str());
            c.filterType     = lp.value("filterType", c.filterType);
            c.filterEnvAmt   = lp.value("filterEnvAmt", c.filterEnvAmt);
            c.filterKeyTrack = lp.value("filterKeyTrack", c.filterKeyTrack);
            c.filtAttackSec  = lp.value("filtAttackSec", c.filtAttackSec);
            c.filtDecaySec   = lp.value("filtDecaySec", c.filtDecaySec);
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
            synthLayerExtrasFromJson(lp, c);
        }
    }

    if (j.contains("synthLfos") && j["synthLfos"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(4, j["synthLfos"].size()); ++i) {
            const auto& v = j["synthLfos"][i];
            auto& l = p.synthLfos[i];
            l.enabled = v.value("enabled", l.enabled);
            l.waveType = v.value("waveType", l.waveType);
            l.rateHz = v.value("rateHz", l.rateHz);
            l.depth = v.value("depth", l.depth);
            l.delaySec = v.value("delaySec", l.delaySec);
            l.fadeSec = v.value("fadeSec", l.fadeSec);
            l.destPitch = v.value("destPitch", l.destPitch);
            l.destCutoff = v.value("destCutoff", l.destCutoff);
            l.destVolume = v.value("destVolume", l.destVolume);
            l.destPan = v.value("destPan", l.destPan);
            l.destPhase = v.value("destPhase", l.destPhase);
            l.keySync = v.value("keySync", l.keySync);
        }
        // Program bank's old scalar fields remain available for backward compatibility.
        const auto& l = p.synthLfos[0];
        p.synthLfoEnabled = l.enabled;
        p.synthLfoWave = l.waveType;
        p.synthLfoRate = l.rateHz;
        p.synthLfoDepth = l.depth;
        p.synthLfoPitch = l.destPitch;
        p.synthLfoCutoff = l.destCutoff;
    }
    if (j.contains("synthEnvelopes") && j["synthEnvelopes"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(3, j["synthEnvelopes"].size()); ++i) {
            const auto& v = j["synthEnvelopes"][i];
            auto& e = p.synthEnvelopes[i];
            e.delaySec = v.value("delaySec", e.delaySec);
            e.attackSec = v.value("attackSec", e.attackSec);
            e.holdSec = v.value("holdSec", e.holdSec);
            e.decaySec = v.value("decaySec", e.decaySec);
            e.sustainLevel = v.value("sustainLevel", e.sustainLevel);
            e.releaseSec = v.value("releaseSec", e.releaseSec);
        }
    }

    // Módulo 3: Órgano
    p.organEnabled      = j.value("organEnabled", p.organEnabled);
    p.organModel        = j.value("organModel", p.organModel);
    p.organVolume       = j.value("organVolume", p.organVolume);
    p.organRotaryFast   = j.value("organRotaryFast", p.organRotaryFast);
    p.organDrive        = j.value("organDrive", p.organDrive);
    p.organRotaryMix = j.value("organRotaryMix", p.organRotaryMix);
    p.organRotaryFastRate = std::clamp(j.value("organRotaryFastRate",p.organRotaryFastRate),.6f,1.5f);
    p.organRotaryRamp = std::clamp(j.value("organRotaryRamp",p.organRotaryRamp),.4f,2.5f);
    p.organRotaryStereoWidth = std::clamp(j.value("organRotaryStereoWidth",p.organRotaryStereoWidth),0.f,1.5f);
    p.organVibratoDepth = j.value("organVibratoDepth", p.organVibratoDepth);
    p.organVibratoRate = j.value("organVibratoRate", p.organVibratoRate);
    p.organChorusVoices = j.value("organChorusVoices", p.organChorusVoices);
    p.organChorusTone = j.value("organChorusTone", p.organChorusTone);
    p.organChorusFeedback = j.value("organChorusFeedback", p.organChorusFeedback);
    p.organChorusWidth = j.value("organChorusWidth", p.organChorusWidth);
    p.organChorusMix = j.value("organChorusMix", p.organChorusMix);
    p.organChorusDepth = j.value("organChorusDepth", p.organChorusDepth);
    p.organChorusRate = j.value("organChorusRate", p.organChorusRate);
    p.organChorusEnabled = j.value("organChorusEnabled", p.organChorusEnabled);
    p.organVibratoMode  = j.value("organVibratoMode", p.organVibratoMode);
    p.organPercEnabled  = j.value("organPercEnabled", p.organPercEnabled);
    p.organPercHarmonic = j.value("organPercHarmonic", p.organPercHarmonic);
    p.organPercDecaySec = j.value("organPercDecaySec", p.organPercDecaySec);
    p.organPercLevel    = j.value("organPercLevel", p.organPercLevel);
    if (j.contains("drawbars") && j["drawbars"].is_array()) {
        for (size_t d = 0; d < std::min<size_t>(9, j["drawbars"].size()); ++d) {
            p.drawbars[d] = j["drawbars"][d].get<float>();
        }
    }

    return p;
}
