#include "WorkstationHost.h"
#include "../modules/NordSynthModule.h"
#include "SynthLayerExtras.h"
#include "InsertFxJson.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>

using json = nlohmann::json;

namespace NordDSP {

WorkstationHost::WorkstationHost() : NordPianoEngine() {
    registerAllParameters();
    // Every write (UI, OSC, MIDI) keeps the mappings' pickup state in step.
    registry_.setWriteObserver([this](const std::string& id, float v) { mappings_.noteWritten(id, v); });
}

void WorkstationHost::registerAllParameters() {
    registry_.registerParam("pianoEnabled", 0.0f, 1.0f, [this](float v) { setPianoEnabled(v >= 0.5f); });
    for(int rack=0;rack<STUDIO_RACK_COUNT;++rack){
        const int sourceCount=rack==0?5:rack==1?SYNTH_LAYER_COUNT:rack==2?1:3;
        for(int src=0;src<sourceCount;++src)for(int slot=0;slot<STUDIO_SLOTS;++slot){
            registry_.registerParam("studioSend"+std::to_string(rack)+"_"+std::to_string(src)+"_"+std::to_string(slot),0.f,1.f,
                [this,rack,src,slot](float v){setStudioSend(rack,src,slot,v);});
        }
    }
    for (int src = 0; src < FX_SOURCE_COUNT; ++src) {
        for (int bus = 0; bus < FX_BUS_COUNT; ++bus) {
            const std::string busName[FX_BUS_COUNT] = {"Mod", "Delay", "Reverb"};
            registry_.registerParam("fxSend" + std::to_string(src) + busName[bus], 0.0f, 1.0f,
                                    [this, src, bus](float v) { setFxSend(src, bus, v); });
        }
    }
    // v31: each piano layer (Sound) has its own insert chain: pianoFx{layer}_{unit}_{field}.
    for(int layer=0;layer<PIANO_LAYER_COUNT;++layer)for(int unit=0;unit<INSERT_FX_UNITS;++unit){
        const std::string fx="pianoFx"+std::to_string(layer)+"_"+std::to_string(unit)+"_";
        auto unitRef=[this,layer,unit]()->SynthLayerFxParams&{return pianoLayerInserts(layer)[size_t(unit)];};
        registry_.registerParam(fx+"on",0.f,1.f,[unitRef](float v){unitRef().enabled=v>=.5f;});
        registry_.registerParam(fx+"rate",.05f,7.f,[unitRef](float v){unitRef().rateHz=v;});
        registry_.registerParam(fx+"depth",0.f,1.f,[unitRef](float v){unitRef().depth=v;});
        registry_.registerParam(fx+"mix",0.f,1.f,[unitRef](float v){unitRef().mix=v;});
        registry_.registerParam(fx+"feedback",-.79f,.79f,[unitRef](float v){unitRef().feedback=v;});
        registry_.registerParam(fx+"time",35.f,750.f,[unitRef](float v){unitRef().timeMs=v;});
        registry_.registerParam(fx+"sync",0.f,1.f,[unitRef](float v){unitRef().syncEnabled=v>=.5f;});
        registry_.registerParam(fx+"division",0.f,15.f,[unitRef](float v){unitRef().syncDivision=std::clamp(int(std::lround(v)),0,15);});
        if(unit==INSERT_REVERB){
            registry_.registerParam(fx+"type",0.f,2.f,[unitRef](float v){unitRef().type=std::clamp(int(std::lround(v)),0,2);});
            registry_.registerParam(fx+"damp",0.f,1.f,[unitRef](float v){unitRef().damping=v;});
            registry_.registerParam(fx+"predelay",0.f,250.f,[unitRef](float v){unitRef().preDelayMs=v;});
        }
    }
    const std::string sampleNames[10] = {"Atk", "Dec", "Sus", "Rel", "Hold", "Pan", "Tune", "Cut", "Res", "Gain"};
    const float sampleMin[10] = {0.001f, 0.02f, 0.f, 0.02f, 0.f, -1.f, -100.f, 40.f, 0.5f, -36.f};
    const float sampleMax[10] = {5.f, 12.f, 1.f, 10.f, 1.f, 1.f, 100.f, 20000.f, 12.f, 18.f};
    for (int layer = 0; layer < PIANO_LAYER_COUNT; ++layer)
        for (int param = 0; param < 10; ++param)
            registry_.registerParam("pianoSample" + std::to_string(layer) + sampleNames[param],
                sampleMin[param], sampleMax[param], [this, layer, param](float v) { setPianoLayerSampleParam(layer, param, v); });
    for(int layer=0;layer<PIANO_LAYER_COUNT;++layer){
        const auto prefix="pianoSample"+std::to_string(layer);
        registry_.registerParam(prefix+"WheelCutoff",-6.f,6.f,[this,layer](float v){setPianoWheelRoute(layer,0,v);});
        registry_.registerParam(prefix+"WheelAmp",-24.f,12.f,[this,layer](float v){setPianoWheelRoute(layer,1,v);});
        registry_.registerParam(prefix+"PortOn",0.f,1.f,[this,layer](float v){setSamplePortamento(layer,0,v);});
        registry_.registerParam(prefix+"PortTime",0.f,2.f,[this,layer](float v){setSamplePortamento(layer,1,v);});
        registry_.registerParam(prefix+"PortMode",0.f,1.f,[this,layer](float v){setSamplePortamento(layer,2,v);});
        registry_.registerParam(prefix+"Mono",0.f,1.f,[this,layer](float v){setPianoVoiceMode(layer,0,v);});
        registry_.registerParam(prefix+"Legato",0.f,1.f,[this,layer](float v){setPianoVoiceMode(layer,1,v);});
        registry_.registerParam(prefix+"WheelCutoffOn",0.f,1.f,[this,layer](float v){setPianoWheelRoute(layer,2,v);});
        registry_.registerParam(prefix+"WheelCutoffInvert",0.f,1.f,[this,layer](float v){setPianoWheelRoute(layer,3,v);});
        registry_.registerParam(prefix+"WheelAmpOn",0.f,1.f,[this,layer](float v){setPianoWheelRoute(layer,4,v);});
        registry_.registerParam(prefix+"WheelVibratoOn",0.f,1.f,[this,layer](float v){setPianoWheelRoute(layer,5,v);});
        registry_.registerParam(prefix+"WheelVibratoDepth",0.f,200.f,[this,layer](float v){setPianoWheelRoute(layer,6,v);});
        registry_.registerParam(prefix+"WheelVibratoRate",.1f,12.f,[this,layer](float v){setPianoWheelRoute(layer,7,v);});
        registry_.registerParam(prefix+"EqHP",20.f,1000.f,[this,layer](float v){setSoundEqParameter(layer,1,v);});
        registry_.registerParam(prefix+"EqLP",1000.f,20000.f,[this,layer](float v){setSoundEqParameter(layer,2,v);});
        // Sound-local three-band EQ (zero-based indices, five filter fields).
        const float eqlo[5]={0.f,20.f,1000.f,12.f,12.f};
        const float eqhi[5]={1.f,1000.f,20000.f,48.f,48.f};
        for(int field=0;field<5;++field)registry_.registerParam(prefix+"Eq"+std::to_string(field),
            eqlo[field],eqhi[field],[this,layer,field](float v){setSoundEqParameter(layer,field,v);});
        for(int band=0;band<3;++band){
            registry_.registerParam(prefix+"EqF"+std::to_string(band),30.f,16000.f,
                [this,layer,band](float v){setSoundEqParameter(layer,5+3*band,v);});
            registry_.registerParam(prefix+"EqQ"+std::to_string(band),.3f,12.f,
                [this,layer,band](float v){setSoundEqParameter(layer,6+3*band,v);});
            registry_.registerParam(prefix+"EqG"+std::to_string(band),-15.f,15.f,
                [this,layer,band](float v){setSoundEqParameter(layer,7+3*band,v);});
        }
    }
    registry_.registerParam("pianoVolume", 0.0f, 1.5f, [this](float v) { setPianoVolume(v); });
    registry_.registerParam("pianoPan",-1.f,1.f,[this](float v){setModulePan(0,v);});
    registry_.registerParam("pianoTune",-100.f,100.f,[this](float v){setModuleTune(0,v);});
    registry_.registerParam("synthPan",-1.f,1.f,[this](float v){setModulePan(1,v);});
    registry_.registerParam("synthTune",-100.f,100.f,[this](float v){setModuleTune(1,v);});
    registry_.registerParam("organPan",-1.f,1.f,[this](float v){setModulePan(2,v);});
    registry_.registerParam("organTune",-100.f,100.f,[this](float v){setModuleTune(2,v);});
    for (int layer = 0; layer < PIANO_LAYER_COUNT; ++layer) {
        registry_.registerParam("pianoLayerEnabled" + std::to_string(layer), 0.0f, 1.0f,
            [this, layer](float v) { setPianoLayerEnabled(layer, v >= 0.5f); });
        registry_.registerParam("pianoLayerVol" + std::to_string(layer), 0.0f, 1.5f,
            [this, layer](float v) { setPianoLayerVolume(layer, v); });
    }
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
    registry_.registerParam("eqEnabled", 0.0f, 1.0f, [this](float v) { setEqEnabled(v >= 0.5f); setStudioEqEnabled(0,v>=.5f); });
    registry_.registerParam("lowCut", 20.0f, 800.0f, [this](float v) { setLowCutHz(v); setStudioEqParam(0,0,v); });
    registry_.registerParam("highCut", 1500.0f, 20000.0f, [this](float v) { setHighCutHz(v); setStudioEqParam(0,1,v); });
    for (int b = 0; b < 5; ++b) {
        registry_.registerParam("eqGain" + std::to_string(b), -15.0f, 15.0f, [this, b](float v) { setEqBandGainDb(b, v); setStudioEq(0,b,v); });
        registry_.registerParam("eqFreq" + std::to_string(b), 20.0f, 20000.0f, [this, b](float v) { setEqBandFreqHz(b, v); setStudioEqParam(0,5+b,v); });
        registry_.registerParam("eqQ" + std::to_string(b), 0.25f, 12.0f, [this, b](float v) { setEqBandQ(b, v); setStudioEqParam(0,10+b,v); });
    }

    // Modulaciones y Delay
    registry_.registerParam("flangerEnabled", 0.f, 1.f, [this](float v) { setFlangerEnabled(v >= .5f); });
    registry_.registerParam("flangerRate", .05f, 5.f, [this](float v) { setFlangerRate(v); });
    registry_.registerParam("flangerDepth", 0.f, 1.f, [this](float v) { setFlangerDepth(v); });
    registry_.registerParam("flangerFeedback", -.85f, .85f, [this](float v) { setFlangerFeedback(v); });
    registry_.registerParam("flangerMix", 0.f, 1.f, [this](float v) { setFlangerMix(v); });
    registry_.registerParam("chorusEnabled", 0.0f, 1.0f, [this](float v) { setChorusEnabled(v >= 0.5f); });
    registry_.registerParam("chorusRate", 0.1f, 6.0f, [this](float v) { setChorusRate(v); });
    registry_.registerParam("chorusDepth", 0.0f, 1.0f, [this](float v) { setChorusDepth(v); });
    registry_.registerParam("chorusMix", 0.0f, 1.0f, [this](float v) { setChorusMix(v); });
    registry_.registerParam("chorusVoices",1.0f,3.0f,[this](float v){setChorusVoices(static_cast<int>(std::lround(v)));});
    registry_.registerParam("chorusWidth",0.0f,1.0f,[this](float v){setChorusWidth(v);});
    registry_.registerParam("chorusFeedback",-0.5f,0.5f,[this](float v){setChorusFeedback(v);});
    registry_.registerParam("chorusTone",500.0f,18000.0f,[this](float v){setChorusTone(v);});
    registry_.registerParam("chorusDelay",7.0f,28.0f,[this](float v){setChorusDelay(v);});
    registry_.registerParam("phaserEnabled", 0.0f, 1.0f, [this](float v) { setPhaserEnabled(v >= 0.5f); });
    registry_.registerParam("phaserRate", 0.1f, 6.0f, [this](float v) { setPhaserRate(v); });
    registry_.registerParam("phaserDepth", 0.0f, 1.0f, [this](float v) { setPhaserDepth(v); });
    registry_.registerParam("phaserFeedback", 0.0f, 0.85f, [this](float v) { setPhaserFeedback(v); });
    registry_.registerParam("phaserMix", 0.0f, 1.0f, [this](float v) { setPhaserMix(v); });
    registry_.registerParam("tremoloEnabled", 0.0f, 1.0f, [this](float v) { setTremoloEnabled(v >= 0.5f); });
    registry_.registerParam("tremoloRate", 0.5f, 14.0f, [this](float v) { setTremoloRate(v); });
    registry_.registerParam("tremoloDepth", 0.0f, 1.0f, [this](float v) { setTremoloDepth(v); });
    registry_.registerParam("masterBpm",30.f,300.f,[this](float v){setMasterBpm(v);});
    registry_.registerParam("delayTempoSync",0.f,1.f,[this](float v){setDelayTempoSync(v>=.5f);});
    registry_.registerParam("delayDivision",0.f,15.f,[this](float v){setDelayDivision(int(std::lround(v)));});
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

    // Módulo 2: Synth de 3 capas, 200 presets y 21 formas de onda (0..20)
    registry_.registerParam("synthEnabled", 0.0f, 1.0f, [this](float v) { multiSynth_.synthEnabled = (v >= 0.5f); });
    registry_.registerParam("synthPresetId", 1.0f, float(SYNTH_FACTORY_PRESET_COUNT), [this](float v) {
       const int id=std::clamp(static_cast<int>(std::lround(v)),1,SYNTH_FACTORY_PRESET_COUNT);
       multiSynth_.loadBuiltInSynthPreset(id);
       // Factory patch FX are part of the factory voice. User programs and custom
       // synth presets can override these settings independently afterward.
       const int family=(id-1)/16;
       const bool spacious=(family==0||family==1||family==8||family==9||family==15);
       setModuleFxParam(true,0,spacious?1.f:0.f);
       setModuleFxParam(true,1, .32f+.065f*(id%8));
       setModuleFxParam(true,3,.26f);
       setModuleFxParam(true,4,(id%7==0)?1.f:0.f);
       setModuleFxParam(true,8,spacious?1.f:0.f);
       setModuleFxParam(true,9,(id%3));
       setModuleFxParam(true,10,.28f);
       setModuleFxParam(true,11,.54f+.045f*(id%8));
       setModuleFxParam(true,12,(family==3&&id%2==0)?1.f:0.f);
       setModuleFxParam(true,13,.18f);
       setModuleFxParam(true,14,(id%11==0)?1.f:0.f);
       for(int layer=0;layer<SYNTH_LAYER_COUNT;layer++){
           setModuleFxSend(layer,0,spacious?.18f+.08f*layer:0.f);
           setModuleFxSend(layer,1,id%7==0?.15f+.07f*layer:0.f);
           setModuleFxSend(layer,2,spacious?.32f+.07f*layer:0.f);
       }
    });
    registry_.registerParam("synthMasterVol", 0.0f, 1.5f, [this](float v) { multiSynth_.synthMasterVol = v; });
    registry_.registerParam("synthMacroCutoff", 0.1f, 2.0f, [this](float v) { multiSynth_.synthMacroCutoff = v; });
    registry_.registerParam("synthWheelGlobalCutoff",-6.f,6.f,[this](float v){multiSynth_.synthWheelGlobalCutoff=v;});
    registry_.registerParam("synthWheelGlobalAmpDb",-24.f,12.f,[this](float v){multiSynth_.synthWheelGlobalAmpDb=v;});
    registry_.registerParam("synthVoiceMaster",0.f,1.f,[this](float v){multiSynth_.synthVoiceMaster=v>=.5f;queueSynthAllNotesOff();});
    for (int m = 0; m < 4; ++m)
        registry_.registerParam("synthMacro" + std::to_string(m + 1), 0.f, 1.f, [this, m](float v) { multiSynth_.macros[size_t(m)] = v; });
    registry_.registerParam("synthSeqClock",0.f,1.f,[this](float v){multiSynth_.synthSeqGlobalClock=v>=.5f;});
    registry_.registerParam("synthSeqShare",0.f,1.f,[this](float v){multiSynth_.synthSeqSharePattern=v>=.5f;});
    registry_.registerParam("synthVoiceMode",0.f,2.f,[this](float v){multiSynth_.synthVoiceMode=std::clamp(static_cast<int>(std::lround(v)),0,2);queueSynthAllNotesOff();});
    registry_.registerParam("synthPortamentoOn",0.f,1.f,[this](float v){multiSynth_.synthPortamentoEnabled=v>=.5f;});
    registry_.registerParam("synthPortamento",0.f,2.f,[this](float v){multiSynth_.synthPortamento=v;});
    registry_.registerParam("synthGlideMode",0.f,1.f,[this](float v){multiSynth_.synthGlideMode=std::clamp(static_cast<int>(std::lround(v)),0,1);});
    registry_.registerParam("synthSlide",-12.f,12.f,[this](float v){multiSynth_.synthSlide=v;});

    for(int module=0;module<3;module++) {
        const char* names[3]={"pianoZone","synthZone","organZone"};
        registry_.registerParam(std::string(names[module])+"Low",0.f,127.f,[this,module](float v){setModuleZone(module,false,int(std::lround(v)));});
        registry_.registerParam(std::string(names[module])+"High",0.f,127.f,[this,module](float v){setModuleZone(module,true,int(std::lround(v)));});
    }
    for(int layer=0;layer<PIANO_LAYER_COUNT;layer++) {
        registry_.registerParam("pianoZone"+std::to_string(layer)+"Low",0.f,127.f,[this,layer](float v){setPianoZone(layer,false,int(std::lround(v)));});
        registry_.registerParam("pianoZone"+std::to_string(layer)+"High",0.f,127.f,[this,layer](float v){setPianoZone(layer,true,int(std::lround(v)));});
    }
    // The same mod/delay/reverb, drive and autopan algorithms are shared across independent racks.
    const char* fxFields[30]={"ChorusOn","ChorusRate","ChorusDepth","ChorusMix","DelayOn","DelayTime","DelayFeedback","DelayMix","ReverbOn","ReverbType","ReverbMix","ReverbSize","DriveOn","DriveAmount","AutoPanOn","AutoPanRate","AutoPanDepth",
       "PhaserOn","PhaserRate","PhaserDepth","PhaserFeedback","PhaserMix","TremoloOn","TremoloRate","TremoloDepth","FlangerOn","FlangerRate","FlangerDepth","FlangerFeedback","FlangerMix"};
    const float fxMin[30]={0,.1,0,0,0,40,0,0,0,0,0,.05,0,0,0,.05,0,0,.1,0,0,0,0,.5,0,0,.05,0,-.85,0};
    const float fxMax[30]={1,6,1,1,1,1200,.85,1,1,3,1,1,1,1,1,12,1,1,6,1,.85,1,1,14,1,1,5,1,.85,1};
    for(int module=0;module<2;module++)for(int k=0;k<30;k++)
        registry_.registerParam(std::string(module==0?"synthFx":"organFx")+fxFields[k],fxMin[k],fxMax[k],
            [this,module,k](float v){setModuleFxParam(module==0,k,v);});
    for(int l=0;l<3;l++)for(int b=0;b<3;b++){
        const char* names[3]={"Mod","Delay","Reverb"};
        registry_.registerParam("synthSend"+std::to_string(l)+names[b],0.f,1.f,
            [this,l,b](float v){setModuleFxSend(l,b,v);});
    }
    for(int bus=0;bus<3;bus++){
        const char* names[3]={"Mod","Delay","Reverb"};
        registry_.registerParam(std::string("organSend")+names[bus],0.f,1.f,
            [this,bus](float v){setOrganFxSend(bus,v);});
    }
    registry_.registerParam("masterDriveOn",0.f,1.f,[this](float v){setMasterColorParam(0,v);});
    registry_.registerParam("masterDriveAmount",0.f,1.f,[this](float v){setMasterColorParam(1,v);});
    registry_.registerParam("masterAutoPanOn",0.f,1.f,[this](float v){setMasterColorParam(2,v);});
    registry_.registerParam("masterAutoPanRate",.05f,12.f,[this](float v){setMasterColorParam(3,v);});
    registry_.registerParam("masterAutoPanDepth",0.f,1.f,[this](float v){setMasterColorParam(4,v);});
    for (int l = 0; l < SYNTH_LAYER_COUNT; ++l) {
        std::string p = "L" + std::to_string(l + 1) + "_";
        // Local independent synth inserts; all parameters available to MIDI/OSC mapper.
        for(int e=0;e<INSERT_FX_UNITS;e++){
            const std::string fx=p+"fx"+std::to_string(e)+"_";
            auto& c=multiSynth_.layers[l].cfg.insertFx[e];(void)c;
            registry_.registerParam(fx+"on",0.f,1.f,[this,l,e](float v){multiSynth_.layers[l].cfg.insertFx[e].enabled=v>=.5f;});
            registry_.registerParam(fx+"rate",.05f,7.f,[this,l,e](float v){multiSynth_.layers[l].cfg.insertFx[e].rateHz=v;});
            registry_.registerParam(fx+"depth",0.f,1.f,[this,l,e](float v){multiSynth_.layers[l].cfg.insertFx[e].depth=v;});
            registry_.registerParam(fx+"mix",0.f,1.f,[this,l,e](float v){multiSynth_.layers[l].cfg.insertFx[e].mix=v;});
            registry_.registerParam(fx+"feedback",-.79f,.79f,[this,l,e](float v){multiSynth_.layers[l].cfg.insertFx[e].feedback=v;});
            registry_.registerParam(fx+"time",35.f,750.f,[this,l,e](float v){multiSynth_.layers[l].cfg.insertFx[e].timeMs=v;});
            registry_.registerParam(fx+"sync",0.f,1.f,[this,l,e](float v){multiSynth_.layers[l].cfg.insertFx[e].syncEnabled=v>=.5f;});
            registry_.registerParam(fx+"division",0.f,15.f,[this,l,e](float v){multiSynth_.layers[l].cfg.insertFx[e].syncDivision=std::clamp(int(std::lround(v)),0,15);});
            if(e==INSERT_REVERB){
                registry_.registerParam(fx+"type",0.f,2.f,[this,l,e](float v){multiSynth_.layers[l].cfg.insertFx[e].type=std::clamp(int(std::lround(v)),0,2);});
                registry_.registerParam(fx+"damp",0.f,1.f,[this,l,e](float v){multiSynth_.layers[l].cfg.insertFx[e].damping=v;});
                registry_.registerParam(fx+"predelay",0.f,250.f,[this,l,e](float v){multiSynth_.layers[l].cfg.insertFx[e].preDelayMs=v;});
            }
        }
        registry_.registerParam(p+"synthType",0.f,float(SYNTH_SYNTHESIS_TYPE_COUNT-1),[this,l](float v){multiSynth_.layers[l].cfg.synthesisType=std::clamp(int(std::lround(v)),0,SYNTH_SYNTHESIS_TYPE_COUNT-1);});
        registry_.registerParam(p+"fmIndex",0.f,8.f,[this,l](float v){multiSynth_.layers[l].cfg.fmIndex=v;});
        registry_.registerParam(p+"fmRatio",.25f,12.f,[this,l](float v){multiSynth_.layers[l].cfg.fmRatio=v;});
        registry_.registerParam(p+"wtMorph",0.f,1.f,[this,l](float v){multiSynth_.layers[l].cfg.wavetableMorph=v;});
        registry_.registerParam(p+"wheelMode",0.f,3.f,[this,l](float v){multiSynth_.layers[l].cfg.wheelMode=std::clamp(int(std::lround(v)),0,3);});
        registry_.registerParam(p+"wheelCutoff",-6.f,6.f,[this,l](float v){multiSynth_.layers[l].cfg.wheelCutoffOctaves=v;});
        registry_.registerParam(p+"wheelAmp",-24.f,12.f,[this,l](float v){multiSynth_.layers[l].cfg.wheelAmpDb=v;});
        registry_.registerParam(p+"wheelVibrato",0.f,200.f,[this,l](float v){multiSynth_.layers[l].cfg.wheelVibratoCents=v;});
        registry_.registerParam(p+"wheelVibRate",.1f,12.f,[this,l](float v){multiSynth_.layers[l].cfg.wheelVibratoRateHz=v;});
        registry_.registerParam(p+"wheelWtMorph",-1.f,1.f,[this,l](float v){multiSynth_.layers[l].cfg.wheelWtMorph=v;});
        registry_.registerParam(p+"voiceMode",0.f,2.f,[this,l](float v){multiSynth_.layers[l].cfg.voiceMode=std::clamp(int(std::lround(v)),0,2);queueSynthAllNotesOff();});
        registry_.registerParam(p+"portamentoOn",0.f,1.f,[this,l](float v){multiSynth_.layers[l].cfg.portamentoEnabled=v>=.5f;});
        registry_.registerParam(p+"portamento",0.f,2.f,[this,l](float v){multiSynth_.layers[l].cfg.portamento=v;});
        registry_.registerParam(p+"glideMode",0.f,1.f,[this,l](float v){multiSynth_.layers[l].cfg.glideMode=std::clamp(int(std::lround(v)),0,1);});
        registry_.registerParam(p+"slide",-12.f,12.f,[this,l](float v){multiSynth_.layers[l].cfg.slide=v;});
        registry_.registerParam(p+"keyLow",0.f,127.f,[this,l](float v){multiSynth_.layers[l].cfg.keyLow=int(std::lround(v));});
        registry_.registerParam(p+"keyHigh",0.f,127.f,[this,l](float v){multiSynth_.layers[l].cfg.keyHigh=int(std::lround(v));});
        registry_.registerParam(p+"keyTrans",-24.f,24.f,[this,l](float v){multiSynth_.layers[l].cfg.keyTranspose=int(std::lround(v));});
        registry_.registerParam(p + "seqOn",0.f,1.f,[this,l](float v){multiSynth_.layers[l].cfg.seqEnabled=v>=.5f;});
        registry_.registerParam(p + "seqBpm",30.f,300.f,[this,l](float v){multiSynth_.layers[l].cfg.seqBpm=v;});
        registry_.registerParam(p + "seqSteps",1.f,16.f,[this,l](float v){multiSynth_.layers[l].cfg.seqSteps=std::clamp(int(std::lround(v)),1,16);});
        registry_.registerParam(p + "seqGate",.05f,1.f,[this,l](float v){multiSynth_.layers[l].cfg.seqGate=v;});
        registry_.registerParam(p + "seqDiv",0.f,5.f,[this,l](float v){multiSynth_.layers[l].cfg.seqDivision=std::clamp(int(std::lround(v)),0,5);});
        registry_.registerParam(p + "seqSync",0.f,1.f,[this,l](float v){multiSynth_.layers[l].cfg.seqSync=v>=.5f;});
        for(int step=0;step<16;++step){
            registry_.registerParam(p+"seqAcc"+std::to_string(step),0.f,1.f,[this,l,step](float v){multiSynth_.layers[l].cfg.seqAccent[size_t(step)]=v>=.5f?1:0;});
            registry_.registerParam(p+"seqSlide"+std::to_string(step),0.f,1.f,[this,l,step](float v){multiSynth_.layers[l].cfg.seqSlide[size_t(step)]=v>=.5f?1:0;});
        }
        for(int step=0;step<16;step++)
            registry_.registerParam(p+"seqStep"+std::to_string(step),-24.f,99.f,
                [this,l,step](float v){int note=int(std::lround(v));multiSynth_.layers[l].cfg.seqNotes[step]=note==99?99:std::clamp(note,-24,24);});
        registry_.registerParam(p + "en",     0.0f, 1.0f,    [this, l](float v) { multiSynth_.layers[l].cfg.enabled = (v >= 0.5f); });
        registry_.registerParam(p + "wave",   0.0f, float(SYNTH_WAVE_COUNT-1), [this, l](float v) { multiSynth_.layers[l].cfg.waveType = std::clamp(static_cast<int>(std::lround(v)), 0, SYNTH_WAVE_COUNT-1); });
        registry_.registerParam(p + "vol",    0.0f, 1.5f,    [this, l](float v) { multiSynth_.layers[l].cfg.volume = v; });
        registry_.registerParam(p + "pan",   -1.0f, 1.0f,    [this, l](float v) { multiSynth_.layers[l].cfg.pan = v; });
        registry_.registerParam(p + "oct",   -2.0f, 2.0f,    [this, l](float v) { multiSynth_.layers[l].cfg.octave = std::clamp(static_cast<int>(std::lround(v)), -2, 2); });
        registry_.registerParam(p + "semi", -12.0f, 12.0f,   [this, l](float v) { multiSynth_.layers[l].cfg.semitones = std::clamp(static_cast<int>(std::lround(v)), -12, 12); });
        registry_.registerParam(p + "det",  -50.0f, 50.0f,   [this, l](float v) { multiSynth_.layers[l].cfg.detuneCents = v; });
        registry_.registerParam(p + "cut",   40.0f, 20000.0f,[this, l](float v) { multiSynth_.layers[l].cfg.cutoffHz = v; });
        registry_.registerParam(p + "res",    0.5f, 12.0f,   [this, l](float v) { multiSynth_.layers[l].cfg.resonanceQ = v; });
        registry_.registerParam(p + "atk",    0.001f, 4.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.attackSec = v; });
        registry_.registerParam(p + "dec",    0.02f, 6.0f,   [this, l](float v) { multiSynth_.layers[l].cfg.decaySec = v; });
        registry_.registerParam(p + "sus",    0.0f, 1.0f,    [this, l](float v) { multiSynth_.layers[l].cfg.sustainLevel = v; });
        registry_.registerParam(p + "rel",    0.02f, 6.0f,   [this, l](float v) { multiSynth_.layers[l].cfg.releaseSec = v; });

        registry_.registerParam(p + "unison",   1.0f, 8.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.unisonVoices = std::clamp(static_cast<int>(std::lround(v)), 1, 8); });
        registry_.registerParam(p + "uniDet",   0.0f, 50.0f, [this, l](float v) { multiSynth_.layers[l].cfg.unisonDetune = v; });
        registry_.registerParam(p + "uniSprd",  0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.unisonSpread = v; });
        registry_.registerParam(p + "phase",    0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.phaseOffset = v; });
        registry_.registerParam(p + "keyTrk",   0.0f, 2.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.keyTrack = v; });
        registry_.registerParam(p + "velCut",   0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.velToCutoff = v; });
        registry_.registerParam(p + "velAmp",   0.0f, 2.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.velToAmp = v; });
        registry_.registerParam(p + "wtIdx",    0.0f, 63.0f, [this, l](float v) { multiSynth_.layers[l].cfg.wavetableIdx = std::clamp(static_cast<int>(std::lround(v)), 0, 63); });
        registry_.registerParam(p + "filtType", 0.0f, float(SYNTH_FILTER_TYPE_COUNT-1), [this, l](float v) { multiSynth_.layers[l].cfg.filterType = std::clamp(static_cast<int>(std::lround(v)), 0, SYNTH_FILTER_TYPE_COUNT-1); });
        registry_.registerParam(p + "filtEnv", -1.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.filterEnvAmt = v; });
        registry_.registerParam(p + "filtKT",   0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.filterKeyTrack = v; });
        registry_.registerParam(p + "fAtk",     0.001f, 4.0f,[this, l](float v) { multiSynth_.layers[l].cfg.filtAttackSec = v; });
        registry_.registerParam(p + "fDec",     0.005f, 10.0f,[this, l](float v) { multiSynth_.layers[l].cfg.filtDecaySec = v; });
        registry_.registerParam(p + "fSus",     0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.filtSustainLvl = v; });
        registry_.registerParam(p + "fRel",     0.005f, 10.0f,[this, l](float v) { multiSynth_.layers[l].cfg.filtReleaseSec = v; });
        registry_.registerParam(p + "saw",      0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.sawLevel = v; });
        registry_.registerParam(p + "pulse",    0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.pulseLevel = v; });
        registry_.registerParam(p + "pw",       0.05f, 0.95f,[this, l](float v) { multiSynth_.layers[l].cfg.pulseWidth = v; });
        registry_.registerParam(p + "pwmDepth", 0.0f, 0.45f, [this, l](float v) { multiSynth_.layers[l].cfg.pwmDepth = v; });
        registry_.registerParam(p + "pwmRate",  0.05f, 12.0f,[this, l](float v) { multiSynth_.layers[l].cfg.pwmRateHz = v; });
        registry_.registerParam(p + "sub",      0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.subLevel = v; });
        registry_.registerParam(p + "noise",    0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.noiseLevel = v; });
        registry_.registerParam(p + "osc2",     0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.osc2Level = v; });
        registry_.registerParam(p + "osc2Det", -50.0f, 50.0f,[this, l](float v) { multiSynth_.layers[l].cfg.osc2Detune = v; });
        registry_.registerParam(p + "osc2Semi",-12.0f, 12.0f,[this, l](float v) { multiSynth_.layers[l].cfg.osc2Semitones = std::clamp(static_cast<int>(std::lround(v)), -12, 12); });
        registry_.registerParam(p + "osc2Wave", 0.0f, 2.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.osc2Wave = std::clamp(static_cast<int>(std::lround(v)), 0, 2); });
        registry_.registerParam(p + "hpf",      20.0f, 6000.0f, [this, l](float v) { multiSynth_.layers[l].cfg.highpassHz = v; });
        registry_.registerParam(p + "drive",    0.0f, 1.0f,  [this, l](float v) { multiSynth_.layers[l].cfg.drive = v; });
        // v31: analog drift, FM4, LA hybrid and attack transient.
        auto& cfg = multiSynth_.layers[l].cfg;
        registry_.registerParam(p + "analog", 0.f, 1.f, [&cfg](float v) { cfg.analog = v; });
        registry_.registerParam(p + "fm4Algo", 0.f, 7.f, [&cfg](float v) { cfg.fm4Algorithm = std::clamp(int(std::lround(v)), 0, 7); });
        registry_.registerParam(p + "fm4Fb", 0.f, 1.f, [&cfg](float v) { cfg.fm4Feedback = v; });
        registry_.registerParam(p + "fm4Vel", 0.f, 1.f, [&cfg](float v) { cfg.fm4VelMod = v; });
        for (int op = 0; op < 4; ++op) {
            const std::string o = p + "op" + std::to_string(op + 1);
            const size_t k = size_t(op);
            registry_.registerParam(o + "Ratio", .5f, 16.f, [&cfg, k](float v) { cfg.opRatio[k] = v; });
            registry_.registerParam(o + "Level", 0.f, 1.f, [&cfg, k](float v) { cfg.opLevel[k] = v; });
            registry_.registerParam(o + "Atk", .001f, 8.f, [&cfg, k](float v) { cfg.opAttack[k] = v; });
            registry_.registerParam(o + "Dec", .01f, 20.f, [&cfg, k](float v) { cfg.opDecay[k] = v; });
            registry_.registerParam(o + "Sus", 0.f, 1.f, [&cfg, k](float v) { cfg.opSustain[k] = v; });
        }
        registry_.registerParam(p + "laMix", 0.f, 1.f, [&cfg](float v) { cfg.laMix = v; });
        registry_.registerParam(p + "laRing", 0.f, 1.f, [&cfg](float v) { cfg.laRing = v; });
        registry_.registerParam(p + "trType", 0.f, float(SYNTH_TRANSIENT_COUNT - 1), [&cfg](float v) { cfg.transientType = std::clamp(int(std::lround(v)), 0, SYNTH_TRANSIENT_COUNT - 1); });
        registry_.registerParam(p + "trLevel", 0.f, 1.f, [&cfg](float v) { cfg.transientLevel = v; });
        registry_.registerParam(p + "trDecay", .005f, 4.f, [&cfg](float v) { cfg.transientDecay = v; });
        registry_.registerParam(p + "trBright", 0.f, 1.f, [&cfg](float v) { cfg.transientTone = v; });
    }

    // Módulo 3: Órgano
    registry_.registerParam("organEnabled", 0.0f, 1.0f, [this](float v) { organModule_.organEnabled = (v >= 0.5f); });
    registry_.registerParam("organVolume", 0.0f, 1.5f, [this](float v) { organModule_.organVolume = v; });
    registry_.registerParam("organRotaryFast", 0.0f, 1.0f, [this](float v) { organModule_.organRotaryFast = (v >= 0.5f); });
    registry_.registerParam("organDrive", 0.0f, 1.0f, [this](float v) { organModule_.organDrive = v; });
    registry_.registerParam("organRotaryMix", 0.0f, 1.0f, [this](float v) { organModule_.rotaryMix = v; });
    registry_.registerParam("organRotaryFastRate", .6f, 1.5f, [this](float v) {organModule_.rotaryFastRate=std::clamp(v,.6f,1.5f);});
    registry_.registerParam("organRotaryRamp", .4f, 2.5f, [this](float v) {organModule_.rotaryRamp=std::clamp(v,.4f,2.5f);});
    registry_.registerParam("organRotaryStereoWidth", 0.f, 1.5f, [this](float v) {organModule_.rotaryStereoWidth=std::clamp(v,0.f,1.5f);});
    registry_.registerParam("organVibratoDepth", 0.0f, 2.0f, [this](float v) { organModule_.vibratoDepth = v; });
    registry_.registerParam("organVibratoRate", 2.0f, 12.0f, [this](float v) { organModule_.vibratoRate = v; });
    registry_.registerParam("organChorusEnabled",0.0f,1.0f,[this](float v){organModule_.chorusEnabled=v>=0.5f;});
    registry_.registerParam("organChorusRate",0.1f,5.0f,[this](float v){organModule_.chorusRate=v;});
    registry_.registerParam("organChorusDepth",0.0f,1.0f,[this](float v){organModule_.chorusDepth=v;});
    registry_.registerParam("organChorusMix",0.0f,1.0f,[this](float v){organModule_.chorusMix=v;});
    registry_.registerParam("organChorusWidth",0.0f,1.0f,[this](float v){organModule_.chorusWidth=v;});
    registry_.registerParam("organChorusFeedback",-0.5f,0.5f,[this](float v){organModule_.chorusFeedback=v;});
    registry_.registerParam("organChorusTone",500.0f,18000.0f,[this](float v){organModule_.chorusTone=v;});
    registry_.registerParam("organChorusVoices",1.0f,3.0f,[this](float v){organModule_.chorusVoices=std::clamp(static_cast<int>(std::lround(v)),1,3);});
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
    for(int layer=0;layer<SYNTH_LAYER_COUNT;++layer){
        const std::string prefix="L"+std::to_string(layer+1)+"_lfo";
        registry_.registerParam(prefix+"Enabled",0.f,1.f,[this,layer](float v){multiSynth_.lfos[layer].enabled=v>=.5f;});
        registry_.registerParam(prefix+"Wave",0.f,float(SYNTH_LFO_WAVE_COUNT-1),[this,layer](float v){multiSynth_.lfos[layer].waveType=std::clamp(int(std::lround(v)),0,SYNTH_LFO_WAVE_COUNT-1);});
        registry_.registerParam(prefix+"Delay",0.f,5.f,[this,layer](float v){multiSynth_.lfos[layer].delaySec=v;});
        registry_.registerParam(prefix+"Fade",0.f,5.f,[this,layer](float v){multiSynth_.lfos[layer].fadeSec=v;});
        registry_.registerParam(prefix+"Sync",0.f,1.f,[this,layer](float v){multiSynth_.lfos[layer].keySync=v>=.5f;});
        registry_.registerParam(prefix+"Rate",.05f,30.f,[this,layer](float v){multiSynth_.lfos[layer].rateHz=v;});
        registry_.registerParam(prefix+"Depth",0.f,1.f,[this,layer](float v){multiSynth_.lfos[layer].depth=v;});
        registry_.registerParam(prefix+"Pitch",-1200.f,1200.f,[this,layer](float v){multiSynth_.lfos[layer].destPitch=int(v);});
        registry_.registerParam(prefix+"Cutoff",-10000.f,10000.f,[this,layer](float v){multiSynth_.lfos[layer].destCutoff=int(v);});
        registry_.registerParam(prefix+"Volume",-1.f,1.f,[this,layer](float v){multiSynth_.lfos[layer].destVolume=v;});
        registry_.registerParam(prefix+"Pan",-1.f,1.f,[this,layer](float v){multiSynth_.lfos[layer].destPan=v;});
    }

    registry_.registerParam("organModel", 0.0f, 7.0f, [this](float v) { organModule_.organModel = std::clamp(static_cast<int>(v), 0, 7); });
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
    for (int i = 0; i < SYNTH_LAYER_COUNT; ++i) {
        const auto& c = multiSynth_.layers[i].cfg;
        arr.push_back({
            {"synthesisType",c.synthesisType},{"fmIndex",c.fmIndex},{"fmRatio",c.fmRatio},{"wavetableMorph",c.wavetableMorph},
            {"dx7Voice",c.synthesisType==5?dx7ToHex(c.dx7Voice):std::string()},{"dx7Name",dx7VoiceName(c.dx7Voice)},{"mt32Program",c.mt32Program},
            {"voiceMode",c.voiceMode},{"portamento",c.portamento},{"portamentoEnabled",c.portamentoEnabled},{"glideMode",c.glideMode},{"slide",c.slide},
            {"keyLow",c.keyLow},{"keyHigh",c.keyHigh},{"keyTranspose",c.keyTranspose},
            {"seqEnabled",c.seqEnabled},{"seqBpm",c.seqBpm},{"seqSteps",c.seqSteps},
            {"seqGate",c.seqGate},{"seqNotes",c.seqNotes},{"seqAccent",c.seqAccent},{"seqSlide",c.seqSlide},
            {"seqDivision",c.seqDivision},{"seqSync",c.seqSync},
            {"insertFx",insertFxToJson(c.insertFx)},
            {"wheelCutoffOctaves",c.wheelCutoffOctaves},{"wheelAmpDb",c.wheelAmpDb},
            {"wheelVibratoCents",c.wheelVibratoCents},{"wheelVibratoRateHz",c.wheelVibratoRateHz},
            {"wheelMode",c.wheelMode},{"wheelWtMorph",c.wheelWtMorph},
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
        synthLayerExtrasToJson(c, arr.back());
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
            {"destPhase", l.destPhase}, {"keySync", l.keySync}
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
    const auto params = getCurrentParams();
    const auto& fx = params.organFx;
    const json ownFx = {
        {"chorusEnabled",fx.chorusEnabled},{"chorusRate",fx.chorusRate},{"chorusDepth",fx.chorusDepth},{"chorusMix",fx.chorusMix},
        {"delayEnabled",fx.delayEnabled},{"delayMs",fx.delayMs},{"delayFeedback",fx.delayFeedback},{"delayMix",fx.delayMix},
        {"reverbEnabled",fx.reverbEnabled},{"reverbType",fx.reverbType},{"reverbMix",fx.reverbMix},{"reverbSize",fx.reverbSize},
        {"driveEnabled",fx.driveEnabled},{"driveAmount",fx.driveAmount},
        {"autoPanEnabled",fx.autoPanEnabled},{"autoPanRate",fx.autoPanRate},{"autoPanDepth",fx.autoPanDepth},
        {"phaserEnabled",fx.phaserEnabled},{"phaserRate",fx.phaserRate},{"phaserDepth",fx.phaserDepth},{"phaserFeedback",fx.phaserFeedback},{"phaserMix",fx.phaserMix},
        {"tremoloEnabled",fx.tremoloEnabled},{"tremoloRate",fx.tremoloRate},{"tremoloDepth",fx.tremoloDepth},
        {"flangerEnabled",fx.flangerEnabled},{"flangerRate",fx.flangerRate},{"flangerDepth",fx.flangerDepth},{"flangerFeedback",fx.flangerFeedback},{"flangerMix",fx.flangerMix}
    };
    return {
        {"organFx",ownFx},
        {"organFxSends",{{"modulation",params.organFxSends.modulation},
                         {"delay",params.organFxSends.delay},{"reverb",params.organFxSends.reverb}}},
        {"organZone",{{"low",params.organZone.low},{"high",params.organZone.high}}},
        {"organEnabled", o.organEnabled}, {"organModel", o.organModel},
        {"organVolume", o.organVolume},{"organPan",params.organPan},{"organTune",params.organTune}, {"organRotaryFast", o.organRotaryFast},
        {"organDrive", o.organDrive}, {"rotaryMix", o.rotaryMix},
        {"rotaryFastRate",o.rotaryFastRate},{"rotaryRamp",o.rotaryRamp},
        {"rotaryStereoWidth",o.rotaryStereoWidth},
        {"vibratoDepth", o.vibratoDepth}, {"vibratoRate", o.vibratoRate},
        {"chorusEnabled",o.chorusEnabled},{"chorusRate",o.chorusRate},
        {"chorusDepth",o.chorusDepth},{"chorusMix",o.chorusMix},
        {"chorusWidth",o.chorusWidth},{"chorusFeedback",o.chorusFeedback},
        {"chorusTone",o.chorusTone},{"chorusVoices",o.chorusVoices},
        {"drawbars", drawbars},
        {"vibratoMode", o.vibratoMode}, {"percussionEnabled", o.percussionEnabled},
        {"percussionHarmonic", o.percussionHarmonic},
        {"percussionDecaySec", o.percussionDecaySec}, {"percussionLevel", o.percussionLevel},
        {"useSamples", o.useSamples}, {"organSamplePath", o.organSamplePath}
    };
}

void WorkstationHost::applySynthLayerJson(size_t i, const json& lp) {
    if (i >= SYNTH_LAYER_COUNT) return;
    auto& c = multiSynth_.layers[i].cfg;
    // A patch that omits a field must not inherit that parameter
    // from the PREVIOUS synth preset.
    c = SynthLayerConfig{};
    insertFxFromJson(lp.value("insertFx",json::array()),c.insertFx);
    c.synthesisType=std::clamp(lp.value("synthesisType",c.synthesisType),0,SYNTH_SYNTHESIS_TYPE_COUNT-1);
    if(lp.contains("dx7Voice") && lp["dx7Voice"].is_string() && dx7FromHex(lp["dx7Voice"].get<std::string>(),c.dx7Voice))
        c.dx7Revision=nextDx7Revision();
    c.mt32Program=std::clamp(lp.value("mt32Program",c.mt32Program),0,127);
    c.fmIndex=std::clamp(lp.value("fmIndex",c.fmIndex),0.f,20.f);c.fmRatio=std::clamp(lp.value("fmRatio",c.fmRatio),.1f,24.f);
    c.wavetableMorph=std::clamp(lp.value("wavetableMorph",c.wavetableMorph),0.f,1.f);
    c.voiceMode=std::clamp(lp.value("voiceMode",c.voiceMode),0,2);
    c.portamento=std::clamp(lp.value("portamento",c.portamento),0.f,2.f);
    c.portamentoEnabled=lp.value("portamentoEnabled",c.portamento>0.001f);
    c.glideMode=lp.value("glideMode",c.glideMode);c.slide=lp.value("slide",c.slide);
    c.keyLow=std::clamp(lp.value("keyLow",c.keyLow),0,127);
    c.keyHigh=std::clamp(lp.value("keyHigh",c.keyHigh),0,127);
    c.keyTranspose=std::clamp(lp.value("keyTranspose",c.keyTranspose),-24,24);
    c.seqEnabled=lp.value("seqEnabled",c.seqEnabled);
    c.seqBpm=std::clamp(lp.value("seqBpm",c.seqBpm),30.f,300.f);
    c.seqSteps=std::clamp(lp.value("seqSteps",c.seqSteps),1,16);
    c.seqGate=std::clamp(lp.value("seqGate",c.seqGate),.05f,1.f);
    c.seqDivision=std::clamp(lp.value("seqDivision",c.seqDivision),0,5);
    c.seqSync=lp.value("seqSync",c.seqSync);
    for(const char* key:{"seqAccent","seqSlide"})
        if(lp.contains(key)&&lp[key].is_array())
            for(size_t k=0;k<std::min<size_t>(16,lp[key].size());++k)
                (std::string(key)=="seqAccent"?c.seqAccent:c.seqSlide)[k]=lp[key][k].is_number()&&lp[key][k].get<int>()!=0?1:0;
    if(lp.contains("seqNotes")&&lp["seqNotes"].is_array())
        for(size_t n=0;n<std::min<size_t>(16,lp["seqNotes"].size());++n)
            if(lp["seqNotes"][n].is_number_integer()){
                const int note=lp["seqNotes"][n].get<int>();c.seqNotes[n]=note==99?99:std::clamp(note,-24,24);
            }
    // Migration: older saved patches used all CC1 destinations simultaneously.
    // Preserve their behavior unless the patch explicitly selects its mode.
    c.wheelMode=std::clamp(lp.value("wheelMode",2),0,3);
    c.wheelCutoffOctaves=std::clamp(lp.value("wheelCutoffOctaves",c.wheelCutoffOctaves),-6.f,6.f);
    c.wheelAmpDb=std::clamp(lp.value("wheelAmpDb",c.wheelAmpDb),-24.f,12.f);
    c.wheelVibratoCents=std::clamp(lp.value("wheelVibratoCents",c.wheelVibratoCents),0.f,200.f);
    c.wheelVibratoRateHz=std::clamp(lp.value("wheelVibratoRateHz",c.wheelVibratoRateHz),.1f,12.f);
    c.wheelWtMorph=std::clamp(lp.value("wheelWtMorph",c.wheelWtMorph),-1.f,1.f);
    c.enabled = lp.value("enabled", c.enabled);
    c.waveType = std::clamp(lp.value("waveType",c.waveType),0,SYNTH_WAVE_COUNT-1);
    c.volume = std::clamp(lp.value("volume",c.volume),0.f,1.5f);
    c.pan = std::clamp(lp.value("pan",c.pan),-1.f,1.f);
    c.octave = std::clamp(lp.value("octave",c.octave),-2,2);
    c.semitones = std::clamp(lp.value("semitones",c.semitones),-12,12);
    c.detuneCents = lp.value("detuneCents", c.detuneCents);
    c.cutoffHz = std::clamp(lp.value("cutoffHz",c.cutoffHz),30.f,20000.f);
    c.resonanceQ = std::clamp(lp.value("resonanceQ",c.resonanceQ),.5f,12.f);
    c.attackSec = std::clamp(lp.value("attackSec",c.attackSec),.001f,10.f);
    c.decaySec = std::clamp(lp.value("decaySec",c.decaySec),.005f,15.f);
    c.sustainLevel = std::clamp(lp.value("sustainLevel",c.sustainLevel),0.f,1.f);
    c.releaseSec = std::clamp(lp.value("releaseSec",c.releaseSec),.005f,12.f);
    c.unisonVoices = std::clamp(lp.value("unisonVoices",c.unisonVoices),1,SYNTH_MAX_UNISON);
    c.unisonDetune = lp.value("unisonDetune", c.unisonDetune);
    c.unisonSpread = lp.value("unisonSpread", c.unisonSpread);
    c.phaseOffset = lp.value("phaseOffset", c.phaseOffset);
    c.keyTrack = lp.value("keyTrack", c.keyTrack);
    c.velToCutoff = lp.value("velToCutoff", c.velToCutoff);
    c.velToAmp = lp.value("velToAmp", c.velToAmp);
    c.wavetableIdx = std::clamp(lp.value("wavetableIdx",c.wavetableIdx),0,63);
    c.samplePath = lp.value("samplePath", c.samplePath);
    c.filterType = std::clamp(lp.value("filterType",c.filterType),0,SYNTH_FILTER_TYPE_COUNT-1);
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
    c.drive = std::clamp(lp.value("drive",c.drive),0.f,1.f);
    synthLayerExtrasFromJson(lp, c);
}

void WorkstationHost::applySynthPresetJson(const json& p) {
    // A preset is a self-contained instrument, not a delta from the previous
    // edit buffer. Missing fields in legacy Factory JSON take factory defaults.
    const SynthPresetParams presetDefaults{};
    const ModuleFxConfig factoryFx{};
    multiSynth_.synthEnabled = p.value("synthEnabled", presetDefaults.synthEnabled);
    multiSynth_.synthMasterVol = p.value("synthMasterVol", presetDefaults.synthMasterVol);
    setModulePan(1,p.value("synthPan",0.f));
    setModuleTune(1,p.value("synthTune",0.f));
    multiSynth_.synthMacroCutoff = p.value("synthMacroCutoff", presetDefaults.synthMacroCutoff);
    multiSynth_.synthWheelGlobalCutoff=std::clamp(p.value("synthWheelGlobalCutoff",presetDefaults.synthWheelGlobalCutoff),-6.f,6.f);
    multiSynth_.synthWheelGlobalAmpDb=std::clamp(p.value("synthWheelGlobalAmpDb",presetDefaults.synthWheelGlobalAmpDb),-24.f,12.f);
    multiSynth_.synthVoiceMaster = p.value("synthVoiceMaster",presetDefaults.synthVoiceMaster);
    // Zone is intrinsic to the Synth preset, not the previously loaded patch.
    const auto zone = p.value("synthZone", json::object());
    setModuleZone(1,false,zone.value("low",0));
    setModuleZone(1,true,zone.value("high",127));
    {
        const json c = p.value("synthFx", json::object());
        const char* names[30]={"chorusEnabled","chorusRate","chorusDepth","chorusMix","delayEnabled","delayMs","delayFeedback","delayMix","reverbEnabled","reverbType","reverbMix","reverbSize","driveEnabled","driveAmount","autoPanEnabled","autoPanRate","autoPanDepth",
          "phaserEnabled","phaserRate","phaserDepth","phaserFeedback","phaserMix","tremoloEnabled","tremoloRate","tremoloDepth","flangerEnabled","flangerRate","flangerDepth","flangerFeedback","flangerMix"};
        const auto& cur=factoryFx;
        const float defaults[30]={float(cur.chorusEnabled),cur.chorusRate,cur.chorusDepth,cur.chorusMix,float(cur.delayEnabled),cur.delayMs,cur.delayFeedback,cur.delayMix,float(cur.reverbEnabled),float(cur.reverbType),cur.reverbMix,cur.reverbSize,float(cur.driveEnabled),cur.driveAmount,float(cur.autoPanEnabled),cur.autoPanRate,cur.autoPanDepth,
          float(cur.phaserEnabled),cur.phaserRate,cur.phaserDepth,cur.phaserFeedback,cur.phaserMix,float(cur.tremoloEnabled),cur.tremoloRate,cur.tremoloDepth,float(cur.flangerEnabled),cur.flangerRate,cur.flangerDepth,cur.flangerFeedback,cur.flangerMix};
        for(int k=0;k<30;k++){
            float value=defaults[k];
            if(c.contains(names[k])) {
                if(c[names[k]].is_boolean()) value=c[names[k]].get<bool>()?1.f:0.f;
                else if(c[names[k]].is_number())value=c[names[k]].get<float>();
            }
            setModuleFxParam(true,k,value);
        }
    }
    {
        const json sends = p.value("synthFxSends", json::array());
        for(size_t i=0;i<3;++i){
            const json v=(sends.is_array() && i<sends.size() && sends[i].is_object())?sends[i]:json::object();
            setModuleFxSend(int(i),0,v.value("modulation",0.f));
            setModuleFxSend(int(i),1,v.value("delay",0.f));
            setModuleFxSend(int(i),2,v.value("reverb",0.f));
        }
    }
    multiSynth_.synthVoiceMode = std::clamp(p.value("synthVoiceMode",presetDefaults.synthVoiceMode),0,2);
    multiSynth_.synthSeqGlobalClock = p.value("synthSeqGlobalClock",false);
    multiSynth_.synthSeqSharePattern = p.value("synthSeqSharePattern",false);
    // A patch without a matrix / macros starts from none (never inherits the previous patch's).
    multiSynth_.macros = {};
    if (p.contains("synthMacros") && p["synthMacros"].is_array())
        for (size_t i = 0; i < std::min<size_t>(4, p["synthMacros"].size()); ++i) multiSynth_.macros[i] = std::clamp(p["synthMacros"][i].get<float>(), 0.f, 1.f);
    {
        std::vector<ModConnectionSpec> mods;
        if (p.contains("modMatrix") && p["modMatrix"].is_array())
            for (const auto& c : p["modMatrix"]) if (c.is_object())
                mods.push_back({c.value("source",0), c.value("target",std::string()), c.value("amount",.25f), c.value("bipolar",false), c.value("curve",0)});
        multiSynth_.setModMatrix(mods);
    }
    multiSynth_.synthPortamento = std::clamp(p.value("synthPortamento",presetDefaults.synthPortamento),0.f,2.f);
    multiSynth_.synthPortamentoEnabled=p.value("synthPortamentoEnabled",presetDefaults.synthPortamentoEnabled);
    multiSynth_.synthGlideMode = std::clamp(p.value("synthGlideMode",presetDefaults.synthGlideMode),0,1);
    multiSynth_.synthSlide = std::clamp(p.value("synthSlide",presetDefaults.synthSlide),-12.f,12.f);
    if (!p.contains("layers"))
        for(auto& layer:multiSynth_.layers)layer.cfg=SynthLayerConfig{};
    if (p.contains("layers") && p["layers"].is_array()) {
        for(size_t i=p["layers"].size();i<SYNTH_LAYER_COUNT;++i)
            multiSynth_.layers[i].cfg = SynthLayerConfig{};
        for (size_t i = 0; i < std::min<size_t>(SYNTH_LAYER_COUNT, p["layers"].size()); ++i) {
            const auto& lp = p["layers"][i];
            applySynthLayerJson(i, lp);
        }
    }
    if (!p.contains("lfos"))
        for(auto& l:multiSynth_.lfos)l=SynthLFO{};
    if (p.contains("lfos") && p["lfos"].is_array()) {
        for(size_t i=p["lfos"].size();i<SYNTH_NUM_LFOS;++i)
            multiSynth_.lfos[i] = SynthLFO{};
        for (size_t i = 0; i < std::min<size_t>(SYNTH_NUM_LFOS, p["lfos"].size()); ++i) {
            const auto& lp = p["lfos"][i];
            auto& l = multiSynth_.lfos[i];
            l = SynthLFO{};
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
            l.keySync = lp.value("keySync", l.keySync);
        }
    }
    if (!p.contains("envelopes"))
        for(auto& e:multiSynth_.envelopes)e=SynthEnvelope{};
    if (p.contains("envelopes") && p["envelopes"].is_array()) {
        for(size_t i=p["envelopes"].size();i<SYNTH_NUM_ENVS;++i)
            multiSynth_.envelopes[i] = SynthEnvelope{};
        for (size_t i = 0; i < std::min<size_t>(SYNTH_NUM_ENVS, p["envelopes"].size()); ++i) {
            const auto& v = p["envelopes"][i];
            auto& e = multiSynth_.envelopes[i];
            e = SynthEnvelope{};
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
    // Organ presets are module-local snapshots. Older partial files receive
    // defaults rather than inheriting effects/drawbars from another preset.
    const NordOrganModule defaults{};
    const ModuleFxConfig defaultFx{};
    if (p.contains("organZone") && p["organZone"].is_object()) {
        const auto& zone = p["organZone"];
        setModuleZone(2,false,zone.value("low",0));
        setModuleZone(2,true,zone.value("high",127));
    }
    {
        const json fx = p.value("organFx",json::object());
        const char* names[30] = {"chorusEnabled","chorusRate","chorusDepth","chorusMix", "delayEnabled","delayMs","delayFeedback","delayMix", "reverbEnabled","reverbType","reverbMix","reverbSize", "driveEnabled","driveAmount", "autoPanEnabled","autoPanRate","autoPanDepth",
            "phaserEnabled","phaserRate","phaserDepth","phaserFeedback","phaserMix","tremoloEnabled","tremoloRate","tremoloDepth","flangerEnabled","flangerRate","flangerDepth","flangerFeedback","flangerMix"};
        const auto& cur = defaultFx;
        const float previous[30] = {float(cur.chorusEnabled),cur.chorusRate,cur.chorusDepth,cur.chorusMix,
            float(cur.delayEnabled),cur.delayMs,cur.delayFeedback,cur.delayMix,
            float(cur.reverbEnabled),float(cur.reverbType),cur.reverbMix,cur.reverbSize,
            float(cur.driveEnabled),cur.driveAmount,float(cur.autoPanEnabled),cur.autoPanRate,cur.autoPanDepth,
            float(cur.phaserEnabled),cur.phaserRate,cur.phaserDepth,cur.phaserFeedback,cur.phaserMix,
            float(cur.tremoloEnabled),cur.tremoloRate,cur.tremoloDepth,
            float(cur.flangerEnabled),cur.flangerRate,cur.flangerDepth,cur.flangerFeedback,cur.flangerMix};
        for (int i=0; i<30; ++i) {
            float next=previous[i];
            if (fx.contains(names[i])) {
                if (fx[names[i]].is_boolean()) next=fx[names[i]].get<bool>()?1.f:0.f;
                else if (fx[names[i]].is_number()) next=fx[names[i]].get<float>();
            }
            setModuleFxParam(false,i,next);
        }
    }
    {
        const json f=p.value("organFxSends",json::object());
        const FxSendLevels cur{};
        setOrganFxSend(0,f.value("modulation",cur.modulation));
        setOrganFxSend(1,f.value("delay",cur.delay));
        setOrganFxSend(2,f.value("reverb",cur.reverb));
    }
    if (!p.contains("organZone")) {setModuleZone(2,false,0);setModuleZone(2,true,127);}
    organModule_.organEnabled = p.value("organEnabled", defaults.organEnabled);
    organModule_.organModel = p.value("organModel", defaults.organModel);
    organModule_.organVolume = p.value("organVolume", defaults.organVolume);
    setModulePan(2,p.value("organPan",0.f));
    setModuleTune(2,p.value("organTune",0.f));
    organModule_.organRotaryFast = p.value("organRotaryFast", defaults.organRotaryFast);
    organModule_.organDrive = p.value("organDrive", defaults.organDrive);
    organModule_.rotaryMix = p.value("rotaryMix", defaults.rotaryMix);
    organModule_.rotaryFastRate=std::clamp(p.value("rotaryFastRate",defaults.rotaryFastRate),.6f,1.5f);
    organModule_.rotaryRamp=std::clamp(p.value("rotaryRamp",defaults.rotaryRamp),.4f,2.5f);
    organModule_.rotaryStereoWidth=std::clamp(p.value("rotaryStereoWidth",defaults.rotaryStereoWidth),0.f,1.5f);
    organModule_.vibratoDepth = p.value("vibratoDepth", defaults.vibratoDepth);
    organModule_.vibratoRate = p.value("vibratoRate", defaults.vibratoRate);
    organModule_.chorusEnabled = p.value("chorusEnabled",defaults.chorusEnabled);
    organModule_.chorusRate = p.value("chorusRate",defaults.chorusRate);
    organModule_.chorusDepth = p.value("chorusDepth",defaults.chorusDepth);
    organModule_.chorusMix = p.value("chorusMix",defaults.chorusMix);
    organModule_.chorusWidth = p.value("chorusWidth",defaults.chorusWidth);
    organModule_.chorusFeedback = p.value("chorusFeedback",defaults.chorusFeedback);
    organModule_.chorusTone = p.value("chorusTone",defaults.chorusTone);
    organModule_.chorusVoices = p.value("chorusVoices",defaults.chorusVoices);
    organModule_.vibratoMode = p.value("vibratoMode", defaults.vibratoMode);
    organModule_.percussionEnabled = p.value("percussionEnabled", defaults.percussionEnabled);
    organModule_.percussionHarmonic = p.value("percussionHarmonic", defaults.percussionHarmonic);
    organModule_.percussionDecaySec = p.value("percussionDecaySec", defaults.percussionDecaySec);
    organModule_.percussionLevel = p.value("percussionLevel", defaults.percussionLevel);
    organModule_.useSamples = p.value("useSamples", defaults.useSamples);
    organModule_.drawbars = defaults.drawbars;
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

std::shared_ptr<const std::vector<Wavetable>> WorkstationHost::getWavetableSnapshot() const {
    return multiSynth_.getWavetableSnapshot();
}

bool WorkstationHost::applyParamById(const std::string& id, float val, bool normalized) {
    return registry_.setParamValue(id, val, normalized);
}

} // namespace NordDSP