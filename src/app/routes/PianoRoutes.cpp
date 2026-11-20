// Generated from the former monolithic main.cpp: route bodies are unchanged.
#include "app/routes/RouteCommon.h"

using namespace SonusDSP;

// Apply all new expression fields together. Used by raw bank reset, legacy
// module presets, per-layer Sounds and automatic Sound recall.
void StageApp::applySoundEq(int layer,const PianoLayerSampleConfig& c){
    engine.setSoundEqParameter(layer,0,c.soundEqEnabled?1.f:0.f);
    engine.setSoundEqParameter(layer,1,c.soundEqHighPass);
    engine.setSoundEqParameter(layer,2,c.soundEqLowPass);
    engine.setSoundEqParameter(layer,3,float(c.soundEqHighPassSlope));
    engine.setSoundEqParameter(layer,4,float(c.soundEqLowPassSlope));
    for(int b=0;b<3;++b){
        engine.setSoundEqParameter(layer,5+3*b,c.soundEqFreq[b]);
        engine.setSoundEqParameter(layer,6+3*b,c.soundEqQ[b]);
        engine.setSoundEqParameter(layer,7+3*b,c.soundEqGain[b]);
    }
}

void StageApp::applyNewSampleRoutes(int layer,const PianoLayerSampleConfig& c){
    engine.setPianoWheelRoute(layer,2,c.wheelCutoffEnabled?1.f:0.f);
    engine.setPianoWheelRoute(layer,3,c.wheelCutoffInvert?1.f:0.f);
    engine.setPianoWheelRoute(layer,4,c.wheelAmpEnabled?1.f:0.f);
    engine.setPianoWheelRoute(layer,5,c.wheelVibratoEnabled?1.f:0.f);
    engine.setPianoWheelRoute(layer,6,c.wheelVibratoCents);
    engine.setPianoWheelRoute(layer,7,c.wheelVibratoRateHz);
    engine.setPianoVoiceMode(layer,0,c.monoMode?1.f:0.f);
    engine.setPianoVoiceMode(layer,1,c.legatoEnabled?1.f:0.f);
}

// A layer Sound owns a single sample source, its ADSR/filter/glide/wheel and
// its own FX sends. Performance mixer/zone and shared rack stay with Program.
json StageApp::capturePianoLayerSound(int layer){
    const auto all=serializeParams(engine.getCurrentParams());
    const auto sources=serializePianoLayerSources(engine);
    return json{{"kind","piano-layer-v1"},{"source",sources[layer]},
                {"sampleConfig",all["pianoSampleConfig"][layer]},
                {"fxSends",all["fxSends"][layer]},
                // Studio FX channel sends are per-layer; inserts/EQ are shared.
                {"studioAux",all["studioRacks"][0]["sends"][layer]},
                // v31: the Sound's own effects (inside sampleConfig.insertFx) and mappings.
                {"mappings",engine.getMappings().exportScope(MapScope::Sound,layer)},
                {"mappingLayer",layer}};
}

bool StageApp::restorePianoLayerSound(int layer,const json& patch){
    if(!patch.is_object()||patch.value("kind",std::string{})!="piano-layer-v1"||
       !patch.contains("source")||!patch["source"].is_object()||
       !patch.contains("sampleConfig")||!patch["sampleConfig"].is_object()||
       !patch.contains("fxSends")||!patch["fxSends"].is_object())return false;
    const auto& src=patch["source"];
    const auto path=src.value("sourcePath",std::string{});
    if(path.empty())return false;
    const auto resolved=resolveSavedSamplePath(fs::path(path));
    if(!fs::is_regular_file(resolved))return false;
    const int sf2Preset=src.value("sf2Preset",-1);
    auto current=engine.getLayerInstrument(layer);
    if(!current||fs::path(current->sourcePath)!=resolved||current->sf2PresetIndex!=sf2Preset){
        auto bank=instPool.getOrLoad(resolved.generic_string(),statusMsg,sf2Preset);
        if(!bank)return false; // do not replace the active layer on load error
        engine.setLayerInstrument(layer,bank);
    }
    // Build a temporary full parameter snapshot but apply ONLY the target
    // layer. Never write other layers, their zones, mixer volume or rack.
    json merged=serializeParams(engine.getCurrentParams());
    merged["pianoSampleConfig"][layer]=patch["sampleConfig"];
    merged["fxSends"][layer]=patch["fxSends"];
    const auto p=deserializeParams(merged,engine.getCurrentParams());
    const auto& c=p.pianoLayerSample[layer];
    const float v[10]={c.attackSec,c.decaySec,c.sustainLevel,c.releaseSec,c.loopHold,
                      c.pan,c.fineTuneCents,c.cutoffHz,c.resonanceQ,c.gainDb};
    for(int k=0;k<10;k++)engine.setPianoLayerSampleParam(layer,k,v[k]);
    engine.setPianoWheelRoute(layer,0,c.wheelCutoffOctaves);
    engine.setPianoWheelRoute(layer,1,c.wheelAmpDb);
    applyNewSampleRoutes(layer,c);
    applySoundEq(layer,c);
    engine.setPianoLayerInserts(layer,c.insertFx);
    // The Sound's mappings follow it to whichever layer it is loaded into.
    engine.getMappings().replaceScope(MapScope::Sound,layer,retargetSoundMappings(patch.value("mappings",json::array()),layer));
    soundDirty[size_t(layer)].store(false);
    syncMappingValues();
    engine.setSamplePortamento(layer,0,c.portamentoEnabled?1.f:0.f);
    engine.setSamplePortamento(layer,1,c.portamentoSec);
    engine.setSamplePortamento(layer,2,float(c.portamentoMode));
    engine.setFxSend(layer,0,p.fxSends[layer].modulation);
    engine.setFxSend(layer,1,p.fxSends[layer].delay);
    engine.setFxSend(layer,2,p.fxSends[layer].reverb);
    if(patch.contains("studioAux")&&patch["studioAux"].is_array()){
        auto cfg=engine.getCurrentParams().studioRacks[0];
        for(size_t i=0;i<std::min<size_t>(STUDIO_SLOTS,patch["studioAux"].size());++i)
            if(patch["studioAux"][i].is_number())
                cfg.sends[layer][i]=std::clamp(patch["studioAux"][i].get<float>(),0.f,1.f);
        engine.setStudioRack(0,cfg);
    }
    engine.setPianoLayerEnabled(layer,true);
    paramRevCounter.fetch_add(1);
    return true;
}

// Stable source association. All lookups are on the control/HTTP thread.
bool StageApp::sourceMatchesSound(const json& sound, const fs::path& path, int sf2Preset){
    if(!sound.is_object()||sound.value("module",std::string{})!="piano"||
       sound.value("scope",std::string{})!="layer")return false;
    const auto patch=sound.value("patch",json::object());
    if(!patch.is_object()||!patch.contains("source")||!patch["source"].is_object())return false;
    const auto& src=patch["source"];
    const auto old=src.value("sourcePath",std::string{});
    if(old.empty())return false;
    return resolveSavedSamplePath(fs::path(old)).lexically_normal()==
        resolveSavedSamplePath(path).lexically_normal() &&
        src.value("sf2Preset",-1)==sf2Preset;
}

const json* StageApp::findDefaultSoundForSource(const fs::path& source,int preset){
    const json* legacy=nullptr;
    int matches=0;
    for(const auto& snd:soundLibrary["sounds"])if(sourceMatchesSound(snd,source,preset)){
        if(snd.value("defaultForSource",false))return &snd;
        ++matches;
        if(!legacy)legacy=&snd;
    }
    return matches==1?legacy:nullptr;
}

json StageApp::loadPresetsFile(const std::string& path){
    json arr = json::array();
    if (fs::exists(path)) {
        try {
            std::ifstream f(path);
            arr = json::parse(f);
        } catch (...) {}
    }
    return arr.is_array()?arr:json::array();
}

bool StageApp::savePresetsFile(const std::string& path, const json& arr){
    return saveJsonAtomic(path,arr);
}

json StageApp::modulePatch(int module){
    const auto all=serializeParams(engine.getCurrentParams());
    json patch=json::object();
    const std::vector<std::string> fields=module==0?
        std::vector<std::string>{"pianoEnabled","pianoVolume","pianoPan","pianoTune","pianoLayers","pianoSampleConfig","pianoZone","pianoLayerZones","fxSends"}:
        module==1?
        std::vector<std::string>{"synthEnabled","synthPresetId","synthMasterVol","synthPan","synthTune","synthMacroCutoff","synthWheelGlobalCutoff","synthWheelGlobalAmpDb","synthOutGainDb","synthVoiceMaster","synthVoiceMode","synthPortamentoEnabled","synthPortamento","synthGlideMode","synthSlide","synthZone","synthFx","synthFxSends",
                                 // v32.4: user / edited presets keep their modulation, macros and sequencer globals
                                 "modMatrix","synthMacros","synthSeqGlobalClock","synthSeqSharePattern"}:
        std::vector<std::string>{"organEnabled","organVolume","organPan","organTune","organZone","organFx","organFxSends"};
    for(const auto& key:fields)if(all.contains(key))patch[key]=all[key];
    patch["studioRack"]=all["studioRacks"][module];
    // v31: a module preset carries its own MIDI / OSC mappings. Every caller saves
    // the patch, so the module no longer has unsaved edits.
    patch["mappings"]=engine.getMappings().exportScope(MapScope::Module,module);
    if(module==0){
        // A Performance is a copy: it keeps each layer's Sound mappings as they are now.
        json sounds=json::array();
        for(int l=0;l<PIANO_LAYER_COUNT;++l)sounds.push_back(engine.getMappings().exportScope(MapScope::Sound,l));
        patch["soundMappings"]=sounds;
        clearAllSoundDirty();
    }
    if(module>=0&&module<=2)moduleDirty[size_t(module)].store(false);
    if(module==0)patch["pianoSources"]=serializePianoLayerSources(engine);
    else if(module==1){
        patch["layers"]=engine.getSynthLayersJson();
        patch["lfos"]=engine.getSynthLFOsJson();
        patch["envelopes"]=engine.getSynthEnvelopesJson();
    }else{
        const auto organ=engine.getOrganParamsJson();
        for(auto it=organ.begin();it!=organ.end();++it)patch[it.key()]=it.value();
    }
    return patch;
}

bool StageApp::restoreModulePatch(int module,const json& patch){
    if(!patch.is_object())return false;
    // A module preset brings its own mappings; one without them has none.
    engine.getMappings().replaceScope(MapScope::Module,module,patch.value("mappings",json::array()));
    if(module==0){
        const json sounds=patch.value("soundMappings",json::array());
        for(int l=0;l<PIANO_LAYER_COUNT;++l)
            engine.getMappings().replaceScope(MapScope::Sound,l,sounds.is_array() && l<int(sounds.size())?sounds[size_t(l)]:json::array());
        clearAllSoundDirty();
    }
    if(module==0){
        // Decode only non-sample parameters here. Cached PCM is reused for
        // unmodified paths; missing files cannot unload other modules.
        if(patch.contains("pianoSources")&&patch["pianoSources"].is_array()){
            json program={{"pianoLayers",patch["pianoSources"]}};
            restorePianoLayerSources(program,engine,libraryRootDir,statusMsg);
        }
        const auto p=deserializeParams(patch,engine.getCurrentParams());
        engine.setPianoEnabled(p.pianoEnabled);engine.setPianoVolume(p.pianoVolume);
        engine.setModulePan(0,p.pianoPan);engine.setModuleTune(0,p.pianoTune);
        engine.setModuleZone(0,false,p.pianoZone.low);engine.setModuleZone(0,true,p.pianoZone.high);
        for(int layer=0;layer<PIANO_LAYER_COUNT;layer++){
            engine.setPianoLayerEnabled(layer,p.pianoLayerEnabled[layer]);
            engine.setPianoLayerVolume(layer,p.pianoLayerVolume[layer]);
            engine.setPianoZone(layer,false,p.pianoLayerZones[layer].low);
            engine.setPianoZone(layer,true,p.pianoLayerZones[layer].high);
            engine.setPianoLayerOctave(layer,p.pianoLayerZones[layer].octave);
            engine.setPianoLayerSemi(layer,p.pianoLayerZones[layer].transpose);
            const auto& c=p.pianoLayerSample[layer];
            const float v[10]={c.attackSec,c.decaySec,c.sustainLevel,c.releaseSec,c.loopHold,
                c.pan,c.fineTuneCents,c.cutoffHz,c.resonanceQ,c.gainDb};
            for(int k=0;k<10;k++)engine.setPianoLayerSampleParam(layer,k,v[k]);
            engine.setPianoWheelRoute(layer,0,c.wheelCutoffOctaves);
            engine.setPianoWheelRoute(layer,1,c.wheelAmpDb);
            applyNewSampleRoutes(layer,c);
            applySoundEq(layer,c);
            engine.setPianoLayerInserts(layer,c.insertFx);
            engine.setSamplePortamento(layer,0,c.portamentoEnabled?1.f:0.f);
            engine.setSamplePortamento(layer,1,c.portamentoSec);
            engine.setSamplePortamento(layer,2,float(c.portamentoMode));
        }
        for(int layer=0;layer<PIANO_LAYER_COUNT;layer++){
            engine.setFxSend(layer,0,p.fxSends[layer].modulation);
            engine.setFxSend(layer,1,p.fxSends[layer].delay);
            engine.setFxSend(layer,2,p.fxSends[layer].reverb);
        }
    }else if(module==1){engine.applySynthPresetJson(patch);}
    else {engine.applyOrganPresetJson(patch);}
    if(patch.contains("studioRack")&&patch["studioRack"].is_object()){
        json temp={{"studioRacks",json::array()}};
        for(int i=0;i<=module;i++)temp["studioRacks"].push_back(i==module?patch["studioRack"]:json(nullptr));
        const auto cfg=deserializeParams(temp,engine.getCurrentParams());
        engine.setStudioRack(module,cfg.studioRacks[module]);
    }else if(patch.contains("studioRacks") && patch["studioRacks"].is_array()){
        const auto cfg=deserializeParams(patch,engine.getCurrentParams());
        engine.setStudioRack(module,cfg.studioRacks[module]);
    }else {
        // A legacy module patch without an FX rack is self-contained.
        // Never inherit the previous preset's chorus, reverb or EQ settings.
        // Only this module's rack is touched; the other modules remain intact.
        engine.setStudioRack(module,StudioRackConfig{});
    }
    if(module>=0&&module<=2)moduleDirty[size_t(module)].store(false);
    syncMappingValues();
    paramRevCounter.fetch_add(1);
    return true;
}

bool StageApp::checkPresetName(std::string& name){
    const auto start=name.find_first_not_of(" \t\r\n");
    if(start==std::string::npos)return false;
    name=name.substr(start,name.find_last_not_of(" \t\r\n")-start+1);
    if(name.size()>64)name.resize(64);
    return true;
}

// v28.2.4: Piano Performances are strictly module-local. A Global Program
// is the only entity permitted to save/recall Piano + Synth + Organ + Master.
// Existing five-layer Piano presets are read as legacy Performances; never
// overwrite or delete their original file during migration.
json StageApp::readPianoPerformances(){
    json performances=loadPresetsFile(pianoPerformancesPath);
    if(!performances.is_array())performances=json::array();
    return performances;
}

void StageApp::registerPianoRoutes() {
    svr.Post("/api/load", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> programGuard(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string targetPath = body.value("path", "");
        int pianoLayer = std::clamp(body.value("layer", 0), 0, PIANO_LAYER_COUNT - 1);

        fs::path p(targetPath);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool ok = false;
        SynthPresetParams keepParams = engine.getCurrentParams();
        if (ext == ".ns3f" || ext == ".ns2p" || ext == ".ns2f" || ext == ".nw2p") {
            PianoInstrument temp;
            ok = BankLoader::loadClaviaProgramPreset(targetPath, temp, statusMsg);
            if (ok) engine.applyPreset(temp.defaultPreset);
        } else {
            auto newInst = instPool.getOrLoad(targetPath, statusMsg,
                ext==".sf2"?body.value("sf2Preset",-1):-1);
            ok = bool(newInst);
            if (ok) {
                engine.setLayerInstrument(pianoLayer, newInst);
                ensurePianoSoundRefs();
                selectedSoundRefs["pianoLayers"][pianoLayer]=nullptr;
                selectedSoundRefs["piano"]=nullptr;
                // A raw file load is a NEW sound: do not carry unsaved envelope,
                // glide, CC1 or send adjustments from the previous Sound.
                const SynthPresetParams defaultSound{};
                const auto& d=defaultSound.pianoLayerSample[pianoLayer];
                const float defaults[10]={d.attackSec,d.decaySec,d.sustainLevel,d.releaseSec,
                    d.loopHold,d.pan,d.fineTuneCents,d.cutoffHz,d.resonanceQ,d.gainDb};
                for(int k=0;k<10;k++)engine.setPianoLayerSampleParam(pianoLayer,k,defaults[k]);
                engine.setPianoWheelRoute(pianoLayer,0,d.wheelCutoffOctaves);
                engine.setPianoWheelRoute(pianoLayer,1,d.wheelAmpDb);
                applyNewSampleRoutes(pianoLayer,d);
                applySoundEq(pianoLayer,d);
                engine.setPianoLayerInserts(pianoLayer,InsertFxChain{});   // a new sample starts dry
                engine.getMappings().replaceScope(MapScope::Sound,pianoLayer,json::array());
                soundDirty[size_t(pianoLayer)].store(false);
                moduleDirty[0].store(true);
                engine.setSamplePortamento(pianoLayer,0,0.f);
                engine.setSamplePortamento(pianoLayer,1,d.portamentoSec);
                engine.setSamplePortamento(pianoLayer,2,float(d.portamentoMode));
                const auto& f=defaultSound.fxSends[pianoLayer];
                engine.setFxSend(pianoLayer,0,f.modulation);
                engine.setFxSend(pianoLayer,1,f.delay);
                engine.setFxSend(pianoLayer,2,f.reverb);
                auto rack=engine.getCurrentParams().studioRacks[0];
                rack.sends[pianoLayer]=defaultSound.studioRacks[0].sends[pianoLayer];
                engine.setStudioRack(0,rack);
                engine.setPianoLayerEnabled(pianoLayer, true);
                engine.setMaxVoices(keepParams.maxVoices);
                // Selecting the same sample in ANY piano layer restores its
                // stored Sound (ENV, mono/legato, CC1 modules and sends).
                // Older Sounds without defaultForSource remain discoverable.
                if(const json* saved=findDefaultSoundForSource(
                     fs::path(newInst->sourcePath),newInst->sf2PresetIndex)){
                    if(restorePianoLayerSound(pianoLayer,(*saved)["patch"])){
                        selectedSoundRefs["pianoLayers"][pianoLayer]=saved->value("id",std::string{});
                    }
                }
            }
        }
        paramRevCounter.fetch_add(1);

        auto activeInst = engine.getInstrument();
        res.set_content(json({
            {"ok", ok},
            {"layer", pianoLayer},
            {"pianoLayers", serializePianoLayerSources(engine)},
            {"message", statusMsg},
            {"instrumentId", activeInst ? activeInst->id : 0},
            {"instrumentName", activeInst ? activeInst->name : "No Sound Loaded"},
            {"sourcePath", activeInst ? fs::path(activeInst->sourcePath).generic_string() : ""},
            {"formatType", activeInst ? activeInst->formatType : "NONE"},
            {"sampleCount", activeInst ? activeInst->strokes.size() : 0},
            {"zoneCount", activeInst ? activeInst->zoneCount : 0},
            {"memoryMB", activeInst ? activeInst->memoryMB : 0.0},
            {"params", serializeParams(engine.getCurrentParams())}
        }).dump(), "application/json");
    });

    svr.Post("/api/piano/layers/clear", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (!body.is_object() || !body.contains("layer") || !body["layer"].is_number_integer()
            || body["layer"].get<int>() < 0 || body["layer"].get<int>() >= PIANO_LAYER_COUNT) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Capa inválida"})", "application/json");
            return;
        }
        const int layer = body["layer"].get<int>();
        std::lock_guard<std::mutex> layerGuard(dataMutex);
        engine.setLayerInstrument(layer, {});
        ensurePianoSoundRefs();
        selectedSoundRefs["pianoLayers"][layer]=nullptr;
        selectedSoundRefs["piano"]=nullptr;
        engine.setPianoLayerEnabled(layer, false);
        engine.getMappings().replaceScope(MapScope::Sound,layer,json::array());
        engine.setPianoLayerInserts(layer,InsertFxChain{});
        soundDirty[size_t(layer)].store(false);
        moduleDirty[0].store(true);
        paramRevCounter.fetch_add(1);
        res.set_content(json({{"ok", true}, {"pianoLayers", serializePianoLayerSources(engine)},
                              {"params", serializeParams(engine.getCurrentParams())}}).dump(), "application/json");
    });

    svr.Get("/api/piano/performances",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        std::lock_guard<std::mutex> guard(dataMutex);
        json entries=readPianoPerformances();
        // Non-destructive legacy compatibility. Canonical names take precedence.
        const auto old=loadPresetsFile(pianoPresetsPath);
        if(old.is_array())for(const auto& entry:old){
            if(!entry.is_object())continue;
            const auto name=entry.value("name",std::string{});
            if(name.empty())continue;
            bool exists=false;
            for(const auto& modern:entries)if(modern.is_object() && modern.value("name",std::string{})==name){exists=true;break;}
            if(!exists)entries.push_back(json{{"name",name},{"legacy",true},{"layerCount",PIANO_LAYER_COUNT}});
        }
        res.set_content(json{{"ok",true},{"performances",entries},{"maxLayers",PIANO_LAYER_COUNT}}.dump(),"application/json");
    });

    svr.Post("/api/piano/performances/store",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()){res.status=400;return;}
        std::string name=b.value("name",std::string{});
        if(!checkPresetName(name)){res.status=400;res.set_content(R"({"ok":false,"error":"Nombre inválido"})","application/json");return;}
        std::lock_guard<std::mutex> guard(dataMutex);
        auto entries=readPianoPerformances();
        json pianoRefs=json::array();
        ensurePianoSoundRefs();
        for(int i=0;i<PIANO_LAYER_COUNT;i++)pianoRefs.push_back(selectedSoundRefs["pianoLayers"][i]);
        const json entry={{"version",1},{"name",name},{"module","piano"},
            {"layerCount",PIANO_LAYER_COUNT},{"patch",modulePatch(0)},
            {"soundRefs",pianoRefs}};   // mappings travel inside "patch" (v31)
        bool replaced=false;
        for(auto& item:entries)if(item.is_object() && item.value("name",std::string{})==name){item=entry;replaced=true;break;}
        if(!replaced)entries.push_back(entry);
        const bool ok=saveJsonAtomic(pianoPerformancesPath,entries);
        if(!ok)res.status=500;
        res.set_content(json{{"ok",ok},{"name",name},{"performances",entries}}.dump(),"application/json");
    });

    svr.Post("/api/piano/performances/load",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()){res.status=400;return;}
        const auto name=b.value("name",std::string{});
        if(name.empty()){res.status=400;return;}
        std::lock_guard<std::mutex> guard(dataMutex);
        json patch, refs, midi, osc;
        bool found=false;
        const auto entries=readPianoPerformances();
        for(const auto& entry:entries)if(entry.is_object() && entry.value("name",std::string{})==name){
            patch=entry.value("patch",json::object());refs=entry.value("soundRefs",json::array());
            midi=entry.value("midiBindings",json::array());osc=entry.value("oscRoutes",json::array());found=true;break;
        }
        if(!found){
            const auto old=loadPresetsFile(pianoPresetsPath);
            if(old.is_array())for(const auto& item:old)if(item.is_object() && item.value("name",std::string{})==name){
                patch=item.value("params",json::object());found=true;break;
            }
        }
        if(!found||!patch.is_object()){
            res.status=404;res.set_content(R"({"ok":false,"error":"Performance de Piano no encontrada"})","application/json");return;
        }
        // Preflight the 5 sample paths. No half-loaded Piano and no modification
        // of Synth/Organ/Master on a missing or undecodable sample.
        if(patch.contains("pianoSources") && patch["pianoSources"].is_array()){
            for(const auto& src:patch["pianoSources"]){
                if(!src.is_object())continue;
                const auto saved=src.value("sourcePath",std::string{});
                if(saved.empty())continue;
                const auto path=resolveSavedSamplePath(fs::path(saved));
                if(!fs::is_regular_file(path) || !instPool.getOrLoad(path.generic_string(),statusMsg,src.value("sf2Preset",-1))){
                    res.status=409;res.set_content(json{{"ok",false},{"error",std::string("Instrumento no disponible: ")+saved}}.dump(),"application/json");return;
                }
            }
        }
        // Legacy performances stored piano bindings next to the patch: migrate them.
        if(!patch.contains("mappings") && (midi.is_array()||osc.is_array())){
            auto& registry=engine.getRegistry();
            json legacy=ControlMappings::migrateLegacyMidi(midi,registry,MapScope::Module,0);
            for(const auto& r:ControlMappings::migrateLegacyOsc(osc,registry,MapScope::Module,0))legacy.push_back(r);
            patch["mappings"]=legacy;
        }
        if(!restoreModulePatch(0,patch)){
            res.status=500;res.set_content(R"({"ok":false,"error":"No se pudo restaurar Piano"})","application/json");return;
        }
        ensurePianoSoundRefs();
        for(int i=0;i<PIANO_LAYER_COUNT;++i)
            selectedSoundRefs["pianoLayers"][i]=refs.is_array() && i<int(refs.size())?refs[i]:json(nullptr);
        paramRevCounter.fetch_add(1);
        res.set_content(json{{"ok",true},{"name",name},{"params",serializeParams(engine.getCurrentParams())},
            {"pianoLayers",serializePianoLayerSources(engine)},{"soundRefs",selectedSoundRefs}}.dump(),"application/json");
    });

    svr.Post("/api/piano/performances/delete",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()){res.status=400;return;}
        const auto name=b.value("name",std::string{});
        std::lock_guard<std::mutex> guard(dataMutex);
        auto entries=readPianoPerformances();
        const auto before=entries.size();
        entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const json& item){
            return item.is_object() && item.value("name",std::string{})==name;
        }),entries.end());
        const bool ok=entries.size()!=before && saveJsonAtomic(pianoPerformancesPath,entries);
        if(!ok)res.status=404;
        res.set_content(json{{"ok",ok}}.dump(),"application/json");
    });

    svr.Get("/api/piano/presets",[this](const httplib::Request&,httplib::Response& res){
        std::lock_guard<std::mutex> lock(dataMutex);res.set_content(loadPresetsFile(pianoPresetsPath).dump(),"application/json");
    });

    svr.Post("/api/piano/presets/save",[this](const httplib::Request& req,httplib::Response& res){
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()){res.status=400;res.set_content(R"({"ok":false})","application/json");return;}
        std::string name=b.value("name",std::string{});
        if(!checkPresetName(name)){res.status=400;res.set_content(R"({"ok":false,"error":"Nombre vacío"})","application/json");return;}
        std::lock_guard<std::mutex> lock(dataMutex);auto presets=loadPresetsFile(pianoPresetsPath);
        if(!presets.is_array())presets=json::array();
        const json preset={{"name",name},{"params",modulePatch(0)}};
        bool replaced=false;for(auto& item:presets)if(item.value("name",std::string{})==name){item=preset;replaced=true;break;}
        if(!replaced)presets.push_back(preset);
        const bool ok=saveJsonAtomic(pianoPresetsPath,presets);if(!ok)res.status=500;
        res.set_content(json{{"ok",ok},{"presets",presets}}.dump(),"application/json");
    });

    svr.Post("/api/piano/presets/load",[this](const httplib::Request& req,httplib::Response& res){
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()||!b.value("name",std::string{}).size()){res.status=400;res.set_content(R"({"ok":false})","application/json");return;}
        std::lock_guard<std::mutex> lock(dataMutex);
        const auto presets=loadPresetsFile(pianoPresetsPath);
        if(presets.is_array())for(const auto& item:presets)if(item.value("name",std::string{})==b.value("name",std::string{})){
            const auto& patch=item.value("params",json::object());
            const bool ok=restoreModulePatch(0,patch);
            if(ok){selectedSoundRefs["piano"]=nullptr;ensurePianoSoundRefs();
                for(auto& ref:selectedSoundRefs["pianoLayers"])ref=nullptr;}
            res.set_content(json{{"ok",ok},{"params",serializeParams(engine.getCurrentParams())},
                    {"pianoLayers",serializePianoLayerSources(engine)}}.dump(),"application/json");return;
        }
        res.status=404;res.set_content(R"({"ok":false,"error":"Preset de piano no encontrado"})","application/json");
    });

    svr.Post("/api/piano/presets/delete",[this](const httplib::Request& req,httplib::Response& res){
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()){res.status=400;res.set_content(R"({"ok":false})","application/json");return;}
        std::lock_guard<std::mutex> lock(dataMutex);auto presets=loadPresetsFile(pianoPresetsPath);
        if(!presets.is_array())presets=json::array();
        const auto target=b.value("name",std::string{});
        presets.erase(std::remove_if(presets.begin(),presets.end(),[&](const json& item){return item.value("name",std::string{})==target;}),presets.end());
        const bool ok=saveJsonAtomic(pianoPresetsPath,presets);if(!ok)res.status=500;
        res.set_content(json{{"ok",ok},{"presets",presets}}.dump(),"application/json");
    });
}
