// Generated from the former monolithic main.cpp: route bodies are unchanged.
#include "app/routes/RouteCommon.h"

using namespace SonusDSP;

// Live engine state shared by GET /api/state and the /ws publisher (everything
// except the edit buffer, which is published separately). Caller holds dataMutex.
json StageApp::liveStateJson() {
        // hot-plug polling happens inside statusJson() (and every 500 ms in maintenanceTick)
        auto inst = engine.getInstrument();

        std::array<uint8_t, 128> ks;
        engine.getActiveKeyStates(ks);
        json activeKeysJson = json::array();
        for (int k = 21; k <= 108; ++k) {
            if (ks[k] != 0) {
                activeKeysJson.push_back({{"k", k}, {"s", ks[k]}});
            }
        }

        auto hwJson = hw->statusJson();

        json j = {
            {"activeProgramSlot", activeProgramSlot},
            {"programName", currentProgramName},
            {"paramRev", paramRevCounter.load()+engine.getRegistry().valueRevision()},
            {"mappingRev", engine.getMappings().revision()},
            {"moduleDirty", json::array({moduleDirty[0].load(),moduleDirty[1].load(),moduleDirty[2].load()})},
            {"soundDirty", [&]{json a=json::array();for(const auto& d:soundDirty)a.push_back(d.load());return a;}()},
            // Name of the synth / organ preset that is playing (null = factory synth patch / unnamed).
            {"presetRefs", {{"synth", selectedSoundRefs.is_object() ? selectedSoundRefs.value("synthPreset", json()) : json()},
                            {"organ", selectedSoundRefs.is_object() ? selectedSoundRefs.value("organPreset", json()) : json()}}},
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
            // v32.4 live safety telemetry
            {"dspLoad", engine.getDspLoad()}, {"dspPeak", engine.getDspPeak()},
            {"maxRenderMicros", engine.getMaxAudioRenderMicros()}, {"midiDropped", engine.getMidiDroppedEvents()},
            {"midiClockSync", engine.midiClockSync()}, {"midiClockActive", engine.midiClockActive()}, {"midiClockBpm", engine.midiClockBpm()},
            {"audioRecovering", hw ? hw->audioRecovering() : false}, {"audioRecoveries", hw ? hw->audioRecoveries() : 0},
            {"sessionRecovery", sessionRecovery}, {"missingSamples", missingSamples},
            {"modulePeaks", json::array({engine.getModulePeak(0), engine.getModulePeak(1), engine.getModulePeak(2)})},
            {"sourcePeaks", [&]{ json a=json::array(); for(int s=0;s<FX_SOURCE_COUNT;++s)a.push_back(engine.getSourcePeak(s)); return a; }()},
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

// ---- v32.4 program capture / apply (shared by STORE, RECALL and the session autosave)
json StageApp::captureProgramJson(int slot, const std::string& name) {
    auto inst = engine.getInstrument();
    return {
        {"slot", slot + 1},
        {"name", name},
        {"sourcePath", inst ? resolveSavedSamplePath(fs::path(inst->sourcePath)).generic_string() : ""},
        {"fileName", inst ? fs::path(inst->sourcePath).filename().string() : ""},
        {"pianoLayers", serializePianoLayerSources(engine)},
        {"params", serializeParams(engine.getCurrentParams())},
        {"soundRefs", selectedSoundRefs},
        {"soundEdited", soundEditedJson()},     // v35: layers that are variations of their Sound
        // v31: program mappings + a copy of each module's mappings as they are now
        // (independent of the presets they came from).
        {"mappings", programMappingsJson()}
    };
}

void StageApp::applyProgramJson(const json& prog) {
    selectedSoundRefs = prog.value("soundRefs", json{{"piano", nullptr}, {"synth", nullptr}, {"organ", nullptr}});
    ensurePianoSoundRefs();
    restorePianoLayerSources(prog, engine, libraryRootDir, statusMsg);
    if (prog.contains("params")) {
        SynthPresetParams p = deserializeParams(prog["params"], engine.getCurrentParams());
        engine.applyPreset(p);
    }
    restoreProgramMappings(prog);
    for (auto& dirty : moduleDirty) dirty.store(false);
    restoreSoundEdited(prog.value("soundEdited", json()));
    syncMappingValues();
    paramRevCounter.fetch_add(1);
}

void StageApp::registerProgramRoutes() {
    // ---- v32.4 PANIC: every voice fades out (18 / 12 / 7 ms), sustain released, bend and
    // wheel centred. Effect tails keep ringing (a hard cut would click).
    svr.Post("/api/panic", [this](const httplib::Request&, httplib::Response& res) {
        engine.setSustainPedal(false);
        engine.setPitchBend(0.f); engine.setModWheel(0.f);
        engine.allNotesOff();
        std::cout << "[PANIC] all notes off\n";
        res.set_content(R"({"ok":true})", "application/json");
    });
    svr.Post("/api/audio/counters/reset", [this](const httplib::Request&, httplib::Response& res) {
        engine.resetAudioCounters();
        res.set_content(R"({"ok":true})", "application/json");
    });
    // ---- v32.4 system settings: sample-RAM budget (A5) and MIDI CLOCK follow (A2)
    // v32.4 system settings: sample-RAM budget (A5), MIDI CLOCK, web / network access.
    auto systemJson = [this]() {
        json urls = json::array();
        if (lanActive) for (const auto& ip : MidiOscMapper::localAddresses()) urls.push_back("http://" + ip + ":8080");
        return json{{"ok", true}, {"midiClockSync", engine.midiClockSync()},
                    {"sampleMemoryBudgetMB", sampleMemoryBudgetMB.load()}, {"sampleResidentMB", instPool.residentMB()},
                    {"webAccess", webAccess.load()}, {"webAccessForced", webAccessForced.load()},
                    {"lanAccess", lanSetting}, {"lanActive", lanActive},
                    {"restartRequired", lanSetting != lanActive && !httpLanAccess},
                    {"nativeWindow", uiMode == UiMode::Window}, {"lanUrls", urls},
                    // v33 (M1) MIDI safety filters and (V0) who hosts the core.
                    {"midiProgramChannel", engine.midiProgramChannel()},
                    {"midiAllNotesOff", engine.midiAllNotesOffEnabled()},
                    {"host", hw ? hw->hostKind() : "app"}};
    };
    svr.Get("/api/system", [systemJson](const httplib::Request&, httplib::Response& res) {
        res.set_content(systemJson().dump(), "application/json");
    });
    svr.Post("/api/system", [this, systemJson](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        if (body.is_object()) {
            if (body.contains("midiProgramChannel") && body["midiProgramChannel"].is_number_integer())
                engine.setMidiProgramChannel(body["midiProgramChannel"].get<int>());
            if (body.contains("midiAllNotesOff") && body["midiAllNotesOff"].is_boolean())
                engine.setMidiAllNotesOffEnabled(body["midiAllNotesOff"].get<bool>());
            if (body.contains("sampleMemoryBudgetMB") && body["sampleMemoryBudgetMB"].is_number())
                sampleMemoryBudgetMB = std::clamp<uint64_t>(body["sampleMemoryBudgetMB"].get<uint64_t>(), 512, 65536);
            if (body.contains("webAccess") && body["webAccess"].is_boolean()) {
                webAccess = body["webAccess"].get<bool>();
                std::cout << "[HTTP] Acceso desde navegador " << (webAccess ? "ACTIVADO" : "desactivado") << "\n";
            }
            if (body.contains("lanAccess") && body["lanAccess"].is_boolean()) {
                lanSetting = body["lanAccess"].get<bool>();
                std::cout << "[HTTP] Red local " << (lanSetting ? "activada" : "desactivada") << " (al reiniciar)\n";
            }
        }
        saveSystemSettings();
        res.set_content(systemJson().dump(), "application/json");
    });
    // ---- v32.4 OSC device (REAPER-style): feedback target, sender filter, test message
    svr.Get("/api/osc", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(oscMapper.status().dump(), "application/json");
    });
    svr.Post("/api/osc", [this](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        if (!body.is_object()) { res.status = 400; res.set_content(R"({"ok":false})", "application/json"); return; }
        const auto cur = oscMapper.deviceConfig();
        const std::string ip = body.value("deviceIp", cur.deviceIp);
        oscMapper.setDevice(ip, body.value("devicePort", cur.devicePort), body.value("feedback", cur.feedback),
                            body.value("onlyFromDevice", cur.onlyFromDevice));
        if (!ip.empty() && oscMapper.deviceConfig().deviceIp.empty()) {
            res.status = 400; res.set_content(R"({"ok":false,"error":"IP no válida: usa el formato 192.168.1.20"})", "application/json"); return;
        }
        saveSystemSettings();
        auto out = oscMapper.status(); out["ok"] = true;
        res.set_content(out.dump(), "application/json");
    });
    svr.Post("/api/osc/test", [this](const httplib::Request&, httplib::Response& res) {
        const bool ok = oscMapper.sendToDevice("/livestage/ping", 1.f);
        res.status = ok ? 200 : 400;
        res.set_content(ok ? R"({"ok":true})" : R"({"ok":false,"error":"Configura primero la IP del dispositivo"})", "application/json");
    });
    // ---- v32.4 session recovery after an unexpected exit
    svr.Post("/api/session/restore", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        std::ifstream in(sessionDir / "recovery.json");
        json saved = json::parse(in, nullptr, false);
        in.close();
        if (!saved.is_object() || !saved.contains("program")) {
            res.status = 404; res.set_content(R"({"ok":false,"error":"No hay sesión guardada"})", "application/json"); return;
        }
        applyProgramJson(saved["program"]);
        currentProgramName = saved.value("programName", currentProgramName);
        activeProgramSlot = std::clamp(saved.value("activeSlot", activeProgramSlot), 0, kProgramSlots - 1);
        // The restored buffer is unsaved work again: keep it marked as edited.
        for (auto& dirty : moduleDirty) dirty.store(true);
        sessionRecovery = nullptr;
        { std::error_code ec; fs::remove(sessionDir / "recovery.json", ec); }
        std::cout << "[SESSION] restored autosave from " << saved.value("savedAt", std::string("?")) << "\n";
        res.set_content(json({{"ok", true}, {"params", serializeParams(engine.getCurrentParams())}}).dump(), "application/json");
    });
    svr.Post("/api/session/discard", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        sessionRecovery = nullptr;
        { std::error_code ec; fs::remove(sessionDir / "recovery.json", ec); }
        res.set_content(R"({"ok":true})", "application/json");
    });

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

        userPrograms[slotIdx] = captureProgramJson(slotIdx, instName);
        if(!saveJsonAtomic(programsFilePath,userPrograms)){
            res.status=500;res.set_content(R"({"ok":false,"error":"Error al guardar programs.json"})","application/json");return;
        }
        validateSetlist();   // v34: a stored slot replaces its old (possibly missing) references

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
        applyProgramJson(prog);

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
