// Generated from the former monolithic main.cpp: route bodies are unchanged.
#include "app/routes/RouteCommon.h"

using namespace NordDSP;

// Live engine state shared by GET /api/state and the /ws publisher (everything
// except the edit buffer, which is published separately). Caller holds dataMutex.
json StageApp::liveStateJson() {
        hw->pollHotPlugIfNeeded();
        auto inst = engine.getInstrument();

        std::array<uint8_t, 128> ks;
        engine.getActiveKeyStates(ks);
        json activeKeysJson = json::array();
        for (int k = 21; k <= 108; ++k) {
            if (ks[k] != 0) {
                activeKeysJson.push_back({{"k", k}, {"s", ks[k]}});
            }
        }

        auto hwJson = hw->getHardwareStatusJson();

        json j = {
            {"activeProgramSlot", activeProgramSlot},
            {"programName", currentProgramName},
            {"paramRev", paramRevCounter.load()+engine.getRegistry().valueRevision()},
            {"mappingRev", engine.getMappings().revision()},
            {"moduleDirty", json::array({moduleDirty[0].load(),moduleDirty[1].load(),moduleDirty[2].load()})},
            {"soundDirty", [&]{json a=json::array();for(const auto& d:soundDirty)a.push_back(d.load());return a;}()},
            {"pianoLayers", serializePianoLayerSources(engine)},
            {"instrumentId", inst ? inst->id : 0},
            {"instrumentName", inst ? inst->name : "Synth / Organ Stage"},
            {"sourcePath", inst ? fs::path(inst->sourcePath).generic_string() : ""},
            {"formatType", inst ? inst->formatType : "NONE"},
            {"sampleCount", inst ? inst->strokes.size() : 0},
            {"zoneCount", inst ? inst->zoneCount : 0},
            {"memoryMB", inst ? inst->memoryMB : 0.0},
            {"statusMessage", statusMsg},
            {"libraryRoot", libraryRootDir},
            {"activeVoices", engine.getActiveVoiceCount()},
            {"audioUnderflows",engine.getAudioUnderflows()},
            {"audioOverBudgetCallbacks",engine.getAudioOverBudgetCallbacks()},
            {"maxVoices", engine.getMaxVoices()},
            {"peakL", engine.getPeakLeft()},
            {"peakR", engine.getPeakRight()},
            {"clipping", engine.isClipping()},
            {"pedalDown", engine.isPedalDown()},
            {"lastMidiNote", engine.getLastMidiNote()},
            {"lastMidiVel", engine.getLastMidiVel()},
            {"lastStrokeIdx", engine.getLastStrokeIndex()},
            {"midiSeq", engine.getMidiEventSeq()},
            {"synthSeqStep", json::array({engine.multiSynth_.seqStep_[0].load(), engine.multiSynth_.seqStep_[1].load(),
                                          engine.multiSynth_.seqStep_[2].load(), engine.multiSynth_.seqStep_[3].load()})},
            {"modWheel",engine.getModWheel()},
            {"pitchBend",engine.getPitchBend()},
            {"activeKeys", activeKeysJson},
            {"connectedMidiCount", hwJson["connectedEnabledMidiCount"]},
            {"disconnectedMidiCount", hwJson["disconnectedEnabledMidiCount"]},
            {"savedAudioMissing", hwJson["savedAudioMissing"]}
        };
        return j;
}

void StageApp::registerProgramRoutes() {
    svr.Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> programGuard(dataMutex);
        res.set_header("Cache-Control", "no-store, max-age=0");
        json j = liveStateJson();
        j["params"] = serializeParams(engine.getCurrentParams());
        res.set_content(j.dump(), "application/json");
    });

    svr.Get("/api/programs", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        res.set_content(userPrograms.dump(), "application/json");
    });

    svr.Post("/api/programs/store", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        int slotIdx = std::clamp(body.value("slot", 0), 0, kProgramSlots - 1);
        activeProgramSlot = slotIdx;

        auto inst = engine.getInstrument();
        std::string normPath = inst ? resolveSavedSamplePath(fs::path(inst->sourcePath)).generic_string() : "";
        std::string fName = inst ? fs::path(inst->sourcePath).filename().string() : "";
        std::string suggested = inst ? inst->name : ("Program " + std::to_string(slotIdx + 1));
        std::string instName = body.value("name", suggested);
        // Trim/limit user-supplied display names (no HTML injection; UI uses textContent).
        const auto begin = instName.find_first_not_of(" \t\n\r");
        if (begin == std::string::npos) instName = suggested;
        else { const auto end = instName.find_last_not_of(" \t\n\r");
            instName = instName.substr(begin, end - begin + 1); }
        if (instName.size() > 48) instName.resize(48);
        currentProgramName = instName;
        initializedEditBuffer = false;

        userPrograms[slotIdx] = {
            {"slot", slotIdx + 1},
            {"name", instName},
            {"sourcePath", normPath},
            {"fileName", fName},
            {"pianoLayers", serializePianoLayerSources(engine)},
            {"params", serializeParams(engine.getCurrentParams())},
            {"soundRefs",selectedSoundRefs},
            // v31: program mappings + a copy of each module's mappings as they are now
            // (independent of the presets they came from).
            {"mappings",programMappingsJson()}
        };
        if(!saveJsonAtomic(programsFilePath,userPrograms)){
            res.status=500;res.set_content(R"({"ok":false,"error":"Error al guardar programs.json"})","application/json");return;
        }

        res.set_content(json({
            {"ok", true},
            {"slot", slotIdx},
            {"storedName", instName},
            {"programName", currentProgramName},
            {"programs", userPrograms}
        }).dump(), "application/json");
    });

    svr.Post("/api/programs/recall", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        int slotIdx = std::clamp(body.value("slot", 0), 0, kProgramSlots - 1);
        activeProgramSlot = slotIdx;
        const auto& prog = userPrograms[slotIdx];
        currentProgramName = prog.value("name", "Program");
        initializedEditBuffer = false;
        selectedSoundRefs=prog.value("soundRefs",json{{"piano",nullptr},{"synth",nullptr},{"organ",nullptr}});
        ensurePianoSoundRefs();

        restorePianoLayerSources(prog, engine, libraryRootDir, statusMsg);
        if (prog.contains("params")) {
            SynthPresetParams p = deserializeParams(prog["params"], engine.getCurrentParams());
            engine.applyPreset(p);
        }
        restoreProgramMappings(prog);
        for(auto& dirty:moduleDirty)dirty.store(false);
        clearAllSoundDirty();
        syncMappingValues();
        paramRevCounter.fetch_add(1);

        auto activeInst = engine.getInstrument();
        res.set_content(json({
            {"ok", true},
            {"slot", slotIdx},
            {"programName", currentProgramName},
            {"pianoLayers", serializePianoLayerSources(engine)},
            {"instrumentId", activeInst ? activeInst->id : 0},
            {"instrumentName", activeInst ? activeInst->name : prog.value("name", "Program")},
            {"sourcePath", activeInst ? fs::path(activeInst->sourcePath).generic_string() : ""},
            {"zoneCount", activeInst ? activeInst->zoneCount : 0},
            {"memoryMB", activeInst ? activeInst->memoryMB : 0.0},
            {"params", serializeParams(engine.getCurrentParams())},
            {"soundRefs",selectedSoundRefs}
        }).dump(), "application/json");
    });

    // INIT resets only the active edit buffer; it DOES NOT overwrite programs.json.
    // The player explicitly stores the initialized setup with STORE if desired.
    svr.Post("/api/programs/init", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        engine.allNotesOff();
        for (int i = 0; i < PIANO_LAYER_COUNT; ++i) engine.setLayerInstrument(i, {});
        engine.applyPreset(SynthPresetParams{});
        currentProgramName = "INIT GLOBAL PROGRAM";
        initializedEditBuffer = true;
        selectedSoundRefs=json{{"piano",nullptr},{"synth",nullptr},{"organ",nullptr}};
        ensurePianoSoundRefs();
        // INIT clears program and module mappings; SYSTEM mappings stay.
        restoreProgramMappings(json{{"mappings",json::object()}});
        for(auto& dirty:moduleDirty)dirty.store(false);
        clearAllSoundDirty();
        syncMappingValues();
        paramRevCounter.fetch_add(1);
        res.set_content(json({{"ok",true}, {"programName",currentProgramName},
            {"slot",activeProgramSlot}, {"pianoLayers",serializePianoLayerSources(engine)},
            {"params",serializeParams(engine.getCurrentParams())}}).dump(), "application/json");
    });

    svr.Post("/api/programs/import", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto source = json::parse(req.body, nullptr, false);
        auto arr = source.is_object() ? source.value("programs",json()) : source;
        bool valid=arr.is_array()&&(arr.size()==static_cast<size_t>(kProgramSlots)||arr.size()==16);
        if(valid)for(size_t i=0;i<arr.size();++i){
            const auto& item=arr[i];
            if(!item.is_object()||!item.contains("params")||!item["params"].is_object()||
               !item.value("name",std::string{}).size()||item.value("name",std::string{}).size()>128){valid=false;break;}
        }
        if (valid) {
            // Accepts 8-slot banks and legacy 16-slot exports (first 8 slots are used).
            userPrograms = json::array();
            for(int i=0;i<kProgramSlots;++i){json slot=arr[i];slot["slot"]=i+1;userPrograms.push_back(std::move(slot));}
            if(!saveJsonAtomic(programsFilePath,userPrograms)){
                res.status=500;res.set_content(R"({"ok":false,"error":"No se pudo guardar programs.json"})","application/json");return;
            }
            res.set_content(json({{"ok", true}, {"programs", userPrograms}}).dump(), "application/json");
            return;
        }
        res.set_content("{\"ok\":false}", "application/json");
    });

    // Parameter catalogue (ranges for the UI, owning module for mapping scopes).
    // Mappings themselves: /api/mappings (routes/MappingRoutes.cpp).
    svr.Get("/api/control",[this](const httplib::Request&,httplib::Response& res){
        json catalog=engine.getRegistry().exportParameterCatalog();
        for(auto& entry:catalog)entry["module"]=parameterModule(entry.value("id",std::string{}));
        res.set_header("Cache-Control","no-store");
        res.set_content(json{{"ok",true},{"parameters",catalog},{"oscRunning",oscMapper.isOscRunning()},
            {"oscPort",oscMapper.getOscPort()},{"lastOscAddress",oscMapper.getLastOscAddress()}}.dump(),"application/json");
    });
}
