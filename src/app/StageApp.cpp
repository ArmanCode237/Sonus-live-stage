#include "app/UserDataPaths.h"
#include "dsp/Mt32Engine.h"
#include "app/StageApp.h"
#include <random>
#include "loaders/DecodedSampleCache.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>

void bootStep(const char* msg) {
    std::cerr << "[BOOT] " << msg << std::endl;
    std::ofstream trace("startup_trace.log", std::ios::app);
    if(trace) trace << msg << std::endl;
}

StageApp::StageApp(int argc, char* argv[], StageAppOptions options) : options_(std::move(options)) {
    bootStep("Motor DSP creado y buffers inicializados");
    {
        // MT-32 ROMs (the user's own dumps) must be loaded before the audio device
        // opens: the synth layers create their emulated MT-32s at that sample rate.
        const auto projectRoms = (fs::path(findUiDir()).parent_path() / "roms/mt32").generic_string();
        const auto userRoms = (stageUserDataDir() / "roms/mt32").generic_string();
        std::error_code ec; fs::create_directories(userRoms, ec);
        SonusDSP::Mt32Roms::load({projectRoms, userRoms});
        bootStep(SonusDSP::Mt32Roms::status().message.c_str());
    }
    // OSC listens on a fixed port: the app only (several plug-in instances would collide).
    if(!options_.plugin && SonusDSP::Edition::kCanMap) oscMapper.startOscServer(9000);

    // Flags ("--lan") are not positional; the first other argument is the library.
    libraryRootDir = "./samples";
    for(int i=1;i<argc;++i){
        const std::string arg=argv[i];
        if(arg=="--lan")httpLanAccess=true;
        else if(arg.rfind("--",0)!=0){libraryRootDir=arg;break;}
    }
    for(int i=1;i<argc;++i){
        const std::string arg=argv[i];
        if(arg=="--lan")httpLanAccess=true;                       // after the library arg too
        else if(arg=="--headless")uiMode=UiMode::Headless;        // server only (tests, remote)
        else if(arg=="--browser")uiMode=UiMode::Browser;          // the console in the browser
        else if(arg=="--web")webAccessForced=true;                // browsers allowed this session
    }
    if(uiMode!=UiMode::Window)webAccessForced=true;               // they reach the API over HTTP
    {   // v32.4 per-session token of the native window (128 random bits, hex)
        std::random_device rd; static const char* hex="0123456789abcdef";
        for(int i=0;i<32;++i)sessionToken.push_back(hex[rd()%16]);
    }
    httpBindAddress=httpLanAccess?"0.0.0.0":"127.0.0.1";
    if (!fs::exists(libraryRootDir)) libraryRootDir = ".";

    // Original samples stay where the user keeps them; cached PCM is derived data.
    sampleLibraryConfig={{"version",1},{"roots",json::array()},
        {"files",json::array()}, {"cacheDir",(stageUserDataDir()/"pcm-cache").string()}};
    if(fs::is_regular_file(sampleLibraryFile)){
        try {std::ifstream f(sampleLibraryFile);auto previous=json::parse(f);
            if(previous.is_object()&&previous.value("version",0)==1){
                for(const char* key:{"roots","files"})
                    if(previous.contains(key)&&previous[key].is_array())
                        sampleLibraryConfig[key]=previous[key];
                if(previous.contains("cacheDir")&&previous["cacheDir"].is_string())
                    sampleLibraryConfig["cacheDir"]=previous["cacheDir"];
            }
        }catch(...){std::cerr<<"[Library] Índice ilegible; se usarán valores seguros.\n";}
    }
    {auto dir=fs::path(sampleLibraryConfig.value("cacheDir",std::string{}));
     if(dir.empty()||!dir.is_absolute())dir=stageUserDataDir()/"pcm-cache";
     std::error_code ec;fs::create_directories(dir,ec);
     if(ec){dir=stageUserDataDir()/"pcm-cache";fs::create_directories(dir,ec);}
     sampleLibraryConfig["cacheDir"]=dir.string();
     DecodedSampleCache::setDirectory(dir);
     BankLoader::setNpnoCacheDirectory(dir);
    }
    refreshSampleSearchRoots();   // v34: before any program restores its samples
    {
        std::ifstream in(relocationFile);
        if(in)try{
            const auto history=json::parse(in);
            if(history.is_array())for(const auto& e:history){
                if(!e.is_object())continue;
                const auto oldPath=fs::path(e.value("oldRoot",std::string{}));
                const auto newPath=fs::path(e.value("newRoot",std::string{}));
                if(oldPath.is_absolute()&&newPath.is_absolute())
                    relocatedPaths.emplace_back(oldPath.lexically_normal(),newPath.lexically_normal());
            }
        }catch(...){}
    }
    if(fs::is_regular_file(libraryIndexFile))try{
        std::ifstream f(libraryIndexFile);const auto j=json::parse(f);
        if(j.is_object()&&j.value("version",0)==1&&j.contains("items")&&j["items"].is_array()){
            indexedItems=j["items"];indexedSignature=j.value("signature",std::string{});
        }
    }catch(...){indexedItems=json::array();}

    soundLibrary={{"version",2},{"sounds",json::array()}};
    if(fs::is_regular_file(soundsFilePath))try{
        std::ifstream f(soundsFilePath);auto data=json::parse(f);
        if(data.is_object()&&data.value("version",0)==2&&data.contains("sounds")&&data["sounds"].is_array())
            soundLibrary=std::move(data);
    }catch(...){std::cerr<<"[SOUNDS] No se pudo abrir la biblioteca; archivo original conservado.\n";}
    selectedSoundRefs=json{{"piano",nullptr},{"synth",nullptr},{"organ",nullptr}};
    ensurePianoSoundRefs();
    if (fs::exists(programsFilePath)) {
        try {
            std::ifstream pf(programsFilePath);
            userPrograms = json::parse(pf);
        } catch (...) {}
    }

    bootStep("Banco de programas leido: iniciando restauracion del slot 1");
    bool loadedFromProgram1 = false;
    if (userPrograms.is_array() && userPrograms.size() >= 8 && userPrograms[0].is_object()) {
        const auto& prog0 = userPrograms[0];
        if(prog0.contains("soundRefs")&&prog0["soundRefs"].is_object())
            selectedSoundRefs=prog0["soundRefs"];
        ensurePianoSoundRefs();
        loadedFromProgram1 = restorePianoLayerSources(prog0, engine, libraryRootDir, statusMsg);
        bootStep("Muestras del slot 1 restauradas; aplicando parametros");
        if (prog0.contains("params")) {
            engine.applyPreset(deserializeParams(prog0["params"], engine.getCurrentParams()));
        }
        bootStep("Parametros del slot 1 aplicados");
        // A program may intentionally contain only synth / organ, with no piano.
        if (prog0.value("sourcePath", std::string{}).empty()) loadedFromProgram1 = true;
    }

    if (!loadedFromProgram1) {
        if (auto loaded=instPool.getOrLoad(libraryRootDir,statusMsg)) {
            engine.setInstrument(loaded);
            std::cout << "[Inicio] " << statusMsg << "\n";
        }
    }

    // v30: the bank has 8 global programs (2 rows × 4). Older 16-slot banks are
    // copied once, untouched, to programs_16slots_backup.json before trimming.
    if (userPrograms.is_array() && userPrograms.size() > static_cast<size_t>(kProgramSlots)) {
        const fs::path backup = fs::path(programsFilePath).parent_path() / "programs_16slots_backup.json";
        std::error_code ec;
        if (!fs::exists(backup, ec) && !saveJsonAtomic(backup, userPrograms))
            std::cerr << "[PROGRAMS] No se pudo crear la copia de los 16 slots; se conserva el archivo sin recortar\n";
        else {
            json trimmed = json::array();
            for (int i = 0; i < kProgramSlots; ++i) trimmed.push_back(userPrograms[i]);
            userPrograms = std::move(trimmed);
            if (!saveJsonAtomic(programsFilePath, userPrograms))
                std::cerr << "[PROGRAMS] No se pudo guardar el banco de 8 programas\n";
        }
    }

    if (!userPrograms.is_array() || userPrograms.size() < static_cast<size_t>(kProgramSlots)) {
        userPrograms = json::array();
        auto inst = engine.getInstrument();
        std::string normPath = inst ? resolveSavedSamplePath(fs::path(inst->sourcePath)).generic_string() : "";
        for (int i = 0; i < kProgramSlots; ++i) {
            userPrograms.push_back({
                {"slot", i + 1}, {"name", inst ? inst->name : ("Program " + std::to_string(i + 1))},
                {"sourcePath", normPath},
                {"fileName", inst ? fs::path(inst->sourcePath).filename().string() : ""},
                {"pianoLayers", serializePianoLayerSources(engine)},
                {"params", serializeParams(engine.getCurrentParams())}
            });
        }
        if(!saveJsonAtomic(programsFilePath,userPrograms))
            std::cerr<<"[PROGRAMS] No se pudo guardar el archivo en disco\n";
    }

    currentProgramName = userPrograms[0].value("name", std::string("Program 1"));
    validateSetlist();   // v32.4: warn now, not when the pad is pressed on stage

    // Warm all piano layers referenced by setlist slots 2..8. This worker only
    // calls the control-thread InstrumentPool; it never runs in RtAudio.
    std::vector<std::pair<std::string,int>> setlistPaths;
    for(size_t slot=1;slot<std::min<size_t>(8,userPrograms.size());++slot){
        const auto& program=userPrograms[slot];
        if(!program.is_object())continue;
        const auto appendPath=[&](const std::string& saved,const std::string& filename,int sf2Preset){
            const fs::path resolved=resolveProgramSample(saved,filename);
            if(!resolved.empty())setlistPaths.emplace_back(resolved.generic_string(),sf2Preset);
        };
        if(program.contains("pianoLayers") && program["pianoLayers"].is_array()){
            for(const auto& layer:program["pianoLayers"]){
                if(layer.is_object())appendPath(layer.value("sourcePath",std::string{}),
                                                layer.value("fileName",std::string{}),
                                                layer.value("sf2Preset",-1));
            }
        }else appendPath(program.value("sourcePath",std::string{}),
                         program.value("fileName",std::string{}),-1);
    }
    instPool.preloadSetlistAsync(setlistPaths);

    bootStep("Inicializando hardware de audio y MIDI");
    hw = options_.makeBackend ? options_.makeBackend(engine) : makeHostedAudioBackend(engine);
    bootStep("Hardware inicializado");

    if (fs::exists(tagsFilePath)) {
        try {
            std::ifstream tf(tagsFilePath);
            sampleTags = json::parse(tf);
        } catch (...) {}
    }
    if (!sampleTags.is_object()) sampleTags = json::object();

    installHttpGuard();

    for(int i=0;i<kProgramSlots;++i){
        const std::string id=std::string("programSlot")+(i<9?"0":"")+std::to_string(i+1);
        engine.getRegistry().registerParam(id,0.f,1.f,[this,i](float v){
            if(v>=0.5f)requestedMidiProgram.store(i,std::memory_order_release);
        });
    }
    // v32.4 PANIC (mappable to a pad / footswitch) and MIDI CLOCK follow (persisted).
    engine.getRegistry().registerParam("panic",0.f,1.f,[this](float v){
        if(v>=0.5f){ engine.setSustainPedal(false); engine.setPitchBend(0.f); engine.allNotesOff(); }
    });
    engine.getRegistry().registerParam("midiClockSync",0.f,1.f,[this](float v){
        engine.setMidiClockSync(v>=0.5f); saveSystemSettings();
    });
    loadSystemSettings();
    // v32.4 session recovery: the lock file survives only an unexpected exit.
    sessionDir = stageUserDataDir()/"session";
    if(!options_.plugin){   // the DAW project is the plug-in's session (no lock / autosave)
        std::error_code ec; fs::create_directories(sessionDir,ec);
        if(fs::exists(sessionDir/"running.lock") && fs::exists(sessionDir/"autosave.json")){
            // Set the crashed session aside: the new session's autosave must not overwrite it.
            fs::rename(sessionDir/"autosave.json",sessionDir/"recovery.json",ec);
            std::ifstream in(sessionDir/"recovery.json");
            const json saved=json::parse(in,nullptr,false);
            if(saved.is_object() && saved.contains("program")){
                sessionRecovery={{"savedAt",saved.value("savedAt",std::string())},{"programName",saved.value("programName",std::string())}};
                std::cout<<"[SESSION] La sesion anterior termino de forma inesperada: autoguardado disponible ("
                         <<saved.value("savedAt",std::string("?"))<<")\n";
            }
        }
        std::ofstream(sessionDir/"running.lock")<<"running\n";
    }
    // Studio inserts are configurable slots. Only parameter values and bypass can
    // be mapped in real time; FX type changes require building a DSP off-thread.
    for(int rack=0;rack<STUDIO_RACK_COUNT;++rack)for(int slot=0;slot<STUDIO_SLOTS;++slot){
        const auto prefix="studioSlot"+std::to_string(rack)+"_"+std::to_string(slot)+"_";
        for(int param=0;param<STUDIO_PARAM_COUNT;++param)
            engine.getRegistry().registerParam(prefix+"p"+std::to_string(param),0.f,1.f,
                [this,rack,slot,param](float v){engine.setStudioSlot(rack,slot,param<16?param+2:param+3,v);});
        // Bypass changes may allocate audio DSP; intentionally not MIDI-mappable.
    }
    // Program 1 was restored above: load SYSTEM mappings, then program 1's own.
    loadControlMappings();
    if(userPrograms.is_array() && !userPrograms.empty()) restoreProgramMappings(userPrograms[0]);
    engine.getMappings().setWriteListener([this](const std::string& id){ markParamEdited(id); });
    syncMappingValues();

    // Factory files are bundled read-only; user edits live in the user data folder.
    // Legacy user presets are migrated once, without overwriting existing user files.
    for(const auto& [legacy,modern]:{
        std::pair<const char*,std::string>{"synth_presets.json",synthPresetsPath},
        std::pair<const char*,std::string>{"organ_presets.json",organPresetsPath}}){
        std::error_code ec;
        if(!fs::exists(modern)&&fs::exists(legacy))fs::copy_file(legacy,modern,ec);
    }

    registerPageRoutes();
    registerLibraryRoutes();
    registerProgramRoutes();
    registerHardwareRoutes();
    registerPianoRoutes();
    registerSoundRoutes();
    registerSynthRoutes();
    registerDx7Routes();
    registerEngineRoutes();
    registerOrganRoutes();

    // /ws publisher: reads the same data as GET /api/state, under the same mutex.
    hub = std::make_unique<StateHub>(
        [this] { std::lock_guard<std::mutex> lock(dataMutex); return liveStateJson(); },
        [this] { std::lock_guard<std::mutex> lock(dataMutex); return serializeParams(engine.getCurrentParams()); });
    registerRealtimeRoutes();
    registerBrowserRoutes();
    registerMappingRoutes();
    registerMidiRoutingRoutes();
    registerAboutRoutes();
    compileMidiRoutes();
}

StageApp::~StageApp() { stopServices(); }   // threads must stop before the members go

bool StageApp::startServices(const std::string& bindAddress, int port) {
    if(servicesRunning_.exchange(true)) return true;
    httpStopping_=false;
    // MIDI program changes are deferred to a worker so decoding SF2/SFZ cannot
    // block MIDI input or the audio callback. Latest selection wins.
    programWorker_=std::thread([this]{
        while(servicesRunning_.load(std::memory_order_acquire)){
            int requested=requestedMidiProgram.exchange(-1,std::memory_order_acq_rel);
            // v32.4 MIDI Program Change: program n -> slot n mod 8 (channel filter: MidiInput.cpp).
            const int pc=engine.takeProgramChange();
            if(pc>=0)requested=pc%kProgramSlots;
            if(requested>=0 && requested<kProgramSlots){
                std::lock_guard<std::mutex> lock(dataMutex);
                activeProgramSlot=requested;
                const auto& prog=userPrograms[requested];
                currentProgramName=prog.value("name",std::string("Program"));
                initializedEditBuffer=false;
                applyProgramJson(prog);
                std::cout<<"[PROGRAM] "<<(pc>=0?"Program Change ":"Pad ")<<(requested+1)<<": "<<currentProgramName<<"\n";
            }
            // Reclaim banks from finished/retired voices away from ASIO callback.
            engine.collectRetiredInstruments();
            std::this_thread::sleep_for(std::chrono::milliseconds(12));
        }
    });
    // v32.4 maintenance worker: MIDI hot-plug (also with no browser open), audio watchdog,
    // session autosave and the sample-RAM budget. Never on the audio or MIDI threads.
    maintenanceWorker_=std::thread([this]{
        while(servicesRunning_.load(std::memory_order_acquire)){
            maintenanceTick();
            for(int i=0;i<10 && servicesRunning_.load(std::memory_order_acquire);++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    hub->start();
    listenOk_=true;
    if(port<=0){
        // Plug-in: any free loopback port (several instances may run at once).
        servicePort_=svr.bind_to_any_port(bindAddress);
        if(servicePort_<=0){ listenOk_=false; return false; }
        serverThread_=std::thread([this]{ listenOk_=svr.listen_after_bind(); });
    }else{
        servicePort_=port;
        serverThread_=std::thread([this,bindAddress,port]{ listenOk_=svr.listen(bindAddress,port); });
    }
    for(int i=0;i<200 && !svr.is_running() && listenOk_.load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return svr.is_running();
}

void StageApp::stopServices() {
    if(!servicesRunning_.load()) return;
    httpStopping_=true;
    svr.stop();
    if(serverThread_.joinable())serverThread_.join();
    hub->stop();
    servicesRunning_.store(false);
    if(programWorker_.joinable())programWorker_.join();
    if(maintenanceWorker_.joinable())maintenanceWorker_.join();
    if(!options_.plugin) autosaveSessionIfNeeded(true);
    engine.collectRetiredInstruments();
}

std::string StageApp::consoleUrl() const {
    return "http://127.0.0.1:"+std::to_string(servicePort_)+"/?k="+sessionToken;
}

int StageApp::run() {
    bootStep("Iniciando servidor HTTP 8080");
    lanActive=httpLanAccess||lanSetting;
    httpBindAddress=lanActive?"0.0.0.0":"127.0.0.1";
    if(uiMode==UiMode::Window && !nativeWindowAvailable()){uiMode=UiMode::Browser;webAccessForced=true;}
    std::cout << "[GUI] HTTP en " << httpBindAddress << ":8080 · "
              << (lanActive ? "red local activada" : "solo este equipo")
              << " · acceso desde navegador " << (webAllowed() ? "permitido" : "desactivado (solo la ventana de la app)") << "\n";
    // The server runs on its own thread; this (main) thread owns the window and its message loop.
    const bool listening=startServices(httpBindAddress,8080);
    if(!listening){
        listenOk_=false;
#if defined(_WIN32)
        if(uiMode==UiMode::Window)
            MessageBoxW(nullptr,L"No se pudo abrir el puerto 8080.\n\nProbablemente Sonus Live Stage ya está abierto en este equipo.",
                        L"Sonus Live Stage",MB_OK|MB_ICONWARNING);
#endif
    }else if(uiMode==UiMode::Window){
        std::string error;
        bootStep("Abriendo la ventana de Sonus Live Stage");
        if(runNativeWindow(consoleUrl(),windowState,error)){
            std::cout<<"[GUI] Ventana cerrada: deteniendo el servidor\n";
            saveSystemSettings();                       // window size / position / full screen
            httpStopping_=true;
            svr.stop();
        }else{
            // No WebView2 runtime: fall back to the browser (it needs web access this session).
            std::cerr<<"[GUI] Ventana nativa no disponible ("<<error<<"): abriendo el navegador\n";
            webAccessForced=true;
#if defined(_WIN32)
            std::system("start http://localhost:8080");
#elif defined(__APPLE__)
            std::system("open http://localhost:8080");
#endif
        }
        if(serverThread_.joinable())serverThread_.join();
    }else{
        std::cout<<"[GUI] Sonus Live Stage: http://localhost:8080\n";
#if defined(_WIN32)
        if(uiMode==UiMode::Browser)std::system("start http://localhost:8080");
#elif defined(__APPLE__)
        if(uiMode==UiMode::Browser)std::system("open http://localhost:8080");
#endif
        if(serverThread_.joinable())serverThread_.join();
    }
    const bool ok=listenOk_.load();
    stopServices();
    { std::error_code ec; fs::remove(sessionDir/"running.lock",ec); }   // clean exit
    if(!ok) {
        bootStep("ERROR: no se pudo escuchar el puerto HTTP 8080 (¿ya ocupado?)");
        return 5;
    }
    bootStep("Servidor detenido");
    return 0;
}

json StageApp::captureState() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return {{"version", 1}, {"activeSlot", activeProgramSlot}, {"programName", currentProgramName},
            {"program", captureProgramJson(activeProgramSlot, currentProgramName)}};
}

std::string StageApp::programSlotName(int slot) {
    std::lock_guard<std::mutex> lock(dataMutex);
    if(slot<0 || slot>=kProgramSlots || !userPrograms.is_array() || slot>=int(userPrograms.size())) return "Program "+std::to_string(slot+1);
    return userPrograms[slot].value("name",std::string("Program ")+std::to_string(slot+1));
}

void StageApp::restoreState(const json& state) {
    if(!state.is_object() || !state.contains("program") || !state["program"].is_object()) return;
    std::lock_guard<std::mutex> lock(dataMutex);
    activeProgramSlot=std::clamp(state.value("activeSlot",0),0,kProgramSlots-1);
    currentProgramName=state.value("programName",std::string("Program"));
    initializedEditBuffer=false;
    applyProgramJson(state["program"]);
    paramRevCounter.fetch_add(1);
}

// ---------------------------------------------------------------- library

std::string StageApp::canonicalSamplePath(const std::string& str){
    std::error_code ec;const auto path=fs::weakly_canonical(str,ec);
    return ec?std::string{}:path.generic_string();
}

// Only a snapshot is held during directory scans: never lock the audio engine.
std::vector<LibraryEntry> StageApp::registeredEntries(bool includeLegacy){
    json snapshot;
    {std::lock_guard<std::mutex> lock(sampleLibraryMutex);snapshot=sampleLibraryConfig;}
    std::vector<LibraryEntry> result;
    std::set<std::string> seen;
    auto append=[&](const LibraryEntry& entry){
        const auto key=canonicalSamplePath(entry.path);
        if(!key.empty()&&seen.insert(key).second)result.push_back(entry);
    };
    auto scan=[&](const std::string& root){
        if(root.empty())return;
        for(const auto& entry:BankLoader::scanLibraryDirectory(root))append(entry);
    };
    if(includeLegacy)scan(libraryRootDir);
    for(const auto& r:snapshot.value("roots",json::array()))if(r.is_string())scan(r.get<std::string>());
    for(const auto& f:snapshot.value("files",json::array()))if(f.is_string()){
        const fs::path path=f.get<std::string>();
        if(!fs::is_regular_file(path))continue;
        for(const auto& entry:BankLoader::scanLibraryDirectory(path.parent_path().string()))
            if(canonicalSamplePath(entry.path)==canonicalSamplePath(path.string()))append(entry);
    }
    return result;
}

// Persistent metadata index: a rescan is explicit, and changes to the
// registered sources invalidate the signature. The legacy piano browser
// deliberately continues to scan its existing directory when requested.
json StageApp::libraryIndexItems(bool force){
    json config;{std::lock_guard<std::mutex> lock(sampleLibraryMutex);config=sampleLibraryConfig;}
    const std::string signature=libraryRootDir+"|"+config["roots"].dump()+"|"+config["files"].dump();
    {std::lock_guard<std::mutex> lock(libraryIndexMutex);
     if(!force&&indexedSignature==signature&&indexedItems.is_array())return indexedItems;
    }
    json found=json::array();
    for(const auto& item:registeredEntries(true))
        found.push_back({{"name",item.name},{"path",item.path},{"ext",item.extension},
            {"group",item.instrumentGroup},{"sizeBytes",item.sizeBytes}});
    if(saveJsonAtomic(libraryIndexFile,json{{"version",1},{"signature",signature},{"items",found}})){
        std::lock_guard<std::mutex> lock(libraryIndexMutex);
        indexedItems=found;indexedSignature=signature;
    }
    return found;
}

json StageApp::libraryProgressJson(){
    const auto state=sampleLibraryWorker.progress();
    return json{{"running",state.running},{"cancelled",state.cancelled},
        {"total",state.total},{"done",state.done},{"failed",state.failed},
        {"skipped",state.skipped},{"current",state.current},{"lastError",state.error}};
}

// ------------------------------------------------- programs / control maps

// v28.2.1: the five sample layers may reference five different Sounds.
// Legacy module-level soundRefs.piano remains readable and intact.
void StageApp::ensurePianoSoundRefs(){
    if(!selectedSoundRefs.is_object())selectedSoundRefs=json::object();
    if(!selectedSoundRefs.contains("pianoLayers")||!selectedSoundRefs["pianoLayers"].is_array())
        selectedSoundRefs["pianoLayers"]=json::array({nullptr,nullptr,nullptr,nullptr,nullptr});
    while(selectedSoundRefs["pianoLayers"].size()<PIANO_LAYER_COUNT)
        selectedSoundRefs["pianoLayers"].push_back(nullptr);
}

// v31 mappings ----------------------------------------------------------------
namespace {
bool isStudioSlotParam(const json& entry){
    return entry.is_object() && entry.value("paramId",std::string{}).rfind("studioSlot",0)==0;
}
json onlyStudioSlots(const json& arr,bool keep){
    json out=json::array();
    if(arr.is_array())for(const auto& e:arr)if(isStudioSlotParam(e)==keep)out.push_back(e);
    return out;
}
}

void StageApp::loadControlMappings(){
    auto& mappings=engine.getMappings();
    auto& registry=engine.getRegistry();
    json cfg=json::object();
    if(fs::exists(mappingsFilePath)){
        try{std::ifstream f(mappingsFilePath);cfg=json::parse(f);}
        catch(...){std::cerr<<"[CONTROLS] Mappings invalidos: se usan ajustes por defecto\n";cfg=json::object();}
    }
    const int version=cfg.value("version",0);
    if(version>=3){
        mappings.replaceScope(MapScope::System,-1,cfg.value("system",json::array()));
    }else{
        // v2 → v3: every global binding becomes an explicit SYSTEM mapping. The
        // controls the MIDI callback used to apply implicitly (CC 7 volume, CC 10 pan,
        // CC 74 / 71 cutoff / resonance) are added unless their CC was mapped.
        const json legacyMidi=onlyStudioSlots(cfg.value("midi",json::array()),false);
        json system=ControlMappings::migrateLegacyMidi(legacyMidi,registry,MapScope::System,-1);
        for(const auto& route:ControlMappings::migrateLegacyOsc(onlyStudioSlots(cfg.value("osc",json::array()),false),registry,MapScope::System,-1))
            system.push_back(route);
        const std::pair<int,const char*> implicit[]={{7,"volume"},{10,"pan"},{74,"cutoff"},{71,"resonance"}};
        for(const auto& [cc,param]:implicit){
            const bool ccUsed=std::any_of(legacyMidi.begin(),legacyMidi.end(),[cc=cc](const json& b){return b.value("cc",-1)==cc;});
            const bool paramUsed=std::any_of(system.begin(),system.end(),[param=param](const json& a){return a.value("paramId",std::string{})==param;});
            if(!ccUsed&&!paramUsed&&registry.hasParam(param))
                system.push_back({{"paramId",param},{"scope","system"},{"midi",{{"type","cc"},{"channel",0},{"number",cc}}}});
        }
        mappings.replaceScope(MapScope::System,-1,system);
        if(fs::exists(mappingsFilePath)){
            std::error_code ec;
            const auto backup=fs::path(mappingsFilePath).parent_path()/"control_mappings.v2-backup.json";
            fs::copy_file(mappingsFilePath,backup,fs::copy_options::skip_existing,ec);
            std::cout<<"[CONTROLS] Mapeos migrados a v3 (copia de seguridad: "<<backup.string()<<")\n";
        }
        persistControlMaps();
    }
    const int storedPort=cfg.value("port",9000);
    if(SonusDSP::Edition::kCanMap&&storedPort>=1024&&storedPort<=65535&&storedPort!=9000)oscMapper.startOscServer(storedPort);
}

bool StageApp::persistControlMaps(){
    return saveJsonAtomic(mappingsFilePath,json{
        {"version",3},{"system",engine.getMappings().exportScope(MapScope::System)},{"port",oscMapper.getOscPort()}});
}

void StageApp::syncMappingValues(){
    engine.getMappings().syncValues(serializeParams(engine.getCurrentParams()));
}

void StageApp::markParamEdited(const std::string& id){
    if(id=="synthPresetId")return;   // selecting a preset is a load, not an edit
    editEpoch.fetch_add(1,std::memory_order_relaxed);
    const int module=parameterModule(id);
    if(module>=0)moduleDirty[size_t(module)].store(true);
    const int layer=soundLayerOfParam(id);
    if(layer>=0)soundDirty[size_t(layer)].store(true);
}

json StageApp::retargetSoundMappings(const json& mappings,int layer){
    json out=json::array();
    if(mappings.is_array())for(auto item:mappings){
        if(!item.is_object())continue;
        item["paramId"]=retargetSoundParam(item.value("paramId",std::string{}),layer);
        item["layer"]=layer;
        out.push_back(item);
    }
    return out;
}

json StageApp::programMappingsJson(){
    auto& m=engine.getMappings();
    json sounds=json::array();
    for(int l=0;l<PIANO_LAYER_COUNT;++l)sounds.push_back(m.exportScope(MapScope::Sound,l));
    return {{"version",3},{"program",m.exportScope(MapScope::Program)},
            {"modules",json::array({m.exportScope(MapScope::Module,0),m.exportScope(MapScope::Module,1),m.exportScope(MapScope::Module,2)})},
            {"sounds",sounds}};
}

void StageApp::restoreProgramMappings(const json& program){
    auto& m=engine.getMappings();
    if(program.is_object() && program.contains("mappings") && program["mappings"].is_object()){
        const auto& saved=program["mappings"];
        m.replaceScope(MapScope::Program,-1,saved.value("program",json::array()));
        const json modules=saved.value("modules",json::array());
        for(int i=0;i<3;++i)
            m.replaceScope(MapScope::Module,i,modules.is_array() && i<int(modules.size())?modules[size_t(i)]:json::array());
        const json sounds=saved.value("sounds",json::array());
        for(int l=0;l<PIANO_LAYER_COUNT;++l)
            m.replaceScope(MapScope::Sound,l,sounds.is_array() && l<int(sounds.size())?sounds[size_t(l)]:json::array());
        return;
    }
    // Legacy program: only its Studio FX slot mappings were program-specific.
    json program_=json::array();
    if(program.is_object()){
        auto& registry=engine.getRegistry();
        program_=ControlMappings::migrateLegacyMidi(onlyStudioSlots(program.value("midiFxSlotMappings",json::array()),true),registry,MapScope::Program,-1);
        for(const auto& r:ControlMappings::migrateLegacyOsc(onlyStudioSlots(program.value("oscFxSlotRoutes",json::array()),true),registry,MapScope::Program,-1))
            program_.push_back(r);
    }
    m.replaceScope(MapScope::Program,-1,program_);
    for(int i=0;i<3;++i)m.replaceScope(MapScope::Module,i,json::array());
    for(int l=0;l<PIANO_LAYER_COUNT;++l)m.replaceScope(MapScope::Sound,l,json::array());
}

void StageApp::serveHtmlPage(const std::string& file, httplib::Response& res) {
    std::ifstream f(findUiFile(file), std::ios::binary);
    if (!f) {
        res.status = 404;
        res.set_content("<h1>Error: No se encontro " + file + "</h1>", "text/html; charset=utf-8");
        return;
    }
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    res.set_content(content, "text/html; charset=utf-8");
}

// ---------------------------------------------------------------- v32.4 live / studio safety

void StageApp::maintenanceTick(){
    if(hw) hw->poll();
    compileMidiRoutes();                              // v33: ports that appeared since the last tick
    if(!options_.plugin) autosaveSessionIfNeeded(false);
    // A5: release instruments no layer / program / setlist uses once the sample RAM
    // budget is exceeded (least recently used first). Live sets stay in RAM.
    instPool.trimUnused(sampleMemoryBudgetMB);
}

void StageApp::autosaveSessionIfNeeded(bool force){
    if(!SonusDSP::Edition::kCanSave) return;   // demo: nothing persists between sessions
    const uint64_t rev=paramRevCounter.load(), epoch=editEpoch.load();
    if(rev==autosaveRev && epoch==autosaveEpoch) return;
    const auto now=std::chrono::steady_clock::now();
    if(!force && now-lastAutosave<std::chrono::seconds(30)) return;
    std::unique_lock<std::mutex> lock(dataMutex,std::try_to_lock);
    if(!lock.owns_lock()) return;                  // busy (loading): next tick
    const std::time_t t=std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm,&t);
#else
    localtime_r(&t,&tm);
#endif
    char stamp[32]; std::strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M:%S",&tm);
    const json saved={{"savedAt",stamp},{"programName",currentProgramName},{"activeSlot",activeProgramSlot},
                      {"program",captureProgramJson(activeProgramSlot,currentProgramName)}};
    lock.unlock();
    if(saveJsonAtomic((sessionDir/"autosave.json").string(),saved)){
        autosaveRev=rev; autosaveEpoch=epoch; lastAutosave=now;
    }
}

fs::path StageApp::resolveProgramSample(const std::string& saved, const std::string& filename) const {
    std::error_code ec;
    if(!saved.empty()){
        const auto resolved=resolveSavedSamplePath(fs::path(saved));
        if(fs::is_regular_file(resolved,ec))return resolved;
    }
    if(!filename.empty()){
        const auto inRoot=fs::path(libraryRootDir)/filename;
        if(fs::is_regular_file(inRoot,ec))return inRoot;
        if(auto found=findSampleInSearchRoots(filename);!found.empty())return found;
    }
    return {};
}

void StageApp::refreshSampleSearchRoots(){
    std::vector<fs::path> roots;
    {
        std::lock_guard<std::mutex> guard(sampleLibraryMutex);
        if(sampleLibraryConfig.contains("roots")&&sampleLibraryConfig["roots"].is_array())
            for(const auto& r:sampleLibraryConfig["roots"])if(r.is_string())roots.emplace_back(r.get<std::string>());
        // Folders of individually registered files (their siblings are usually the rest of the set).
        if(sampleLibraryConfig.contains("files")&&sampleLibraryConfig["files"].is_array())
            for(const auto& f:sampleLibraryConfig["files"])if(f.is_string())roots.push_back(fs::path(f.get<std::string>()).parent_path());
    }
    std::error_code ec;
    roots.push_back(fs::absolute(fs::path(libraryRootDir),ec));
    roots.push_back(fs::path(findUiDir()).parent_path()/"samples");     // samples shipped with the app
    roots.push_back(stageUserDataDir()/"samples");
    setSampleSearchRoots(roots);
}

void StageApp::validateSetlist(){
    missingSamples=json::array();
    for(size_t slot=0;slot<std::min<size_t>(kProgramSlots,userPrograms.size());++slot){
        const auto& program=userPrograms[slot];
        if(!program.is_object() || !program.contains("pianoLayers") || !program["pianoLayers"].is_array()) continue;
        for(const auto& layer:program["pianoLayers"]){
            if(!layer.is_object()) continue;
            const std::string saved=layer.value("sourcePath",std::string{}), file=layer.value("fileName",std::string{});
            if(saved.empty() && file.empty()) continue;
            // Same resolution as RECALL: saved path, relocations, LIBRARY folders, file name.
            if(resolveProgramSample(saved,file).empty()){
                missingSamples.push_back({{"slot",int(slot)+1},{"program",program.value("name",std::string())},
                                          {"file",file.empty()?fs::path(saved).filename().string():file}});
            }
        }
    }
    for(const auto& m:missingSamples)
        std::cerr<<"[SETLIST] Program "<<m.value("slot",0)<<" ("<<m.value("program",std::string())<<"): falta "<<m.value("file",std::string())<<"\n";
}

void StageApp::loadSystemSettings(){
    std::ifstream in(stageUserFile("system_settings.json"));
    const json s=json::parse(in,nullptr,false);
    if(!s.is_object()) return;
    engine.setMidiClockSync(s.value("midiClockSync",false));
    engine.setMidiProgramChannel(s.value("midiProgramChannel",0));          // v33 (M1): omni by default
    engine.setMidiAllNotesOffEnabled(s.value("midiAllNotesOff",true));
    if(s.contains("midiRouting")&&s["midiRouting"].is_object()&&s["midiRouting"].value("rules",json()).is_array()){
        std::lock_guard<std::mutex> lock(routingMutex_);
        midiRouting_=s["midiRouting"];                  // sanitised on the next POST; compiled below
    }
    sampleMemoryBudgetMB=std::clamp<uint64_t>(s.value("sampleMemoryBudgetMB",uint64_t(4096)),512,65536);
    webAccess=s.value("webAccess",false);
    lanSetting=s.value("lanAccess",false);
    if(s.contains("window")&&s["window"].is_object()){
        const auto& w=s["window"];
        windowState.x=w.value("x",-1);windowState.y=w.value("y",-1);
        windowState.width=w.value("width",1440);windowState.height=w.value("height",900);
        windowState.maximized=w.value("maximized",true);windowState.fullscreen=w.value("fullscreen",false);
    }
    if(s.contains("osc")&&s["osc"].is_object()){
        const auto& o=s["osc"];
        oscMapper.setDevice(o.value("deviceIp",std::string()),o.value("devicePort",9000),
                            o.value("feedback",false),o.value("onlyFromDevice",false));
    }
}

void StageApp::saveSystemSettings(){
    const auto osc=oscMapper.deviceConfig();
    saveJsonAtomic(stageUserFile("system_settings.json"),
        json{{"midiClockSync",engine.midiClockSync()},{"sampleMemoryBudgetMB",sampleMemoryBudgetMB.load()},
             {"midiProgramChannel",engine.midiProgramChannel()},{"midiAllNotesOff",engine.midiAllNotesOffEnabled()},
             {"midiRouting",[this]{ std::lock_guard<std::mutex> lock(routingMutex_); return midiRouting_; }()},
             {"webAccess",webAccess.load()},{"lanAccess",lanSetting},
             {"window",{{"x",windowState.x},{"y",windowState.y},{"width",windowState.width},{"height",windowState.height},
                        {"maximized",windowState.maximized},{"fullscreen",windowState.fullscreen}}},
             {"osc",{{"deviceIp",osc.deviceIp},{"devicePort",osc.devicePort},{"feedback",osc.feedback},{"onlyFromDevice",osc.onlyFromDevice}}}});
}
