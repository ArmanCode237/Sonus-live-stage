#pragma once
#include <filesystem>
#include <chrono>
// Standalone Live Stage application: engine, persistent user data and the
// HTTP control surface. Formerly the body of main(); every former local of
// main() is a member with the same name so route handlers stay unchanged.
//
// Route handlers are grouped by domain in src/app/routes/*Routes.cpp and
// capture only [this]. Lifetime: StageApp outlives the HTTP server (svr is
// the last member, so it is destroyed first).
#include "httplib.h" // must precede <windows.h> (winsock2 ordering)
#include "app/AppCommon.h"
#include "app/ParamSerialization.h"
#include "app/SampleSources.h"
#include "app/StateHub.h"
#include "app/NativeWindow.h"
#include "control/ParamValueReader.h"
#include "app/UserDataPaths.h"
#include "control/MidiOscMapper.h"
#include "core/WorkstationHost.h"
#include "loaders/BankLoader.h"
#include "loaders/SampleLibraryManager.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/AudioBackend.h"

/** v33 (V0): how the core is hosted. The standalone app passes RtAudio / RtMidi devices;
 *  the plug-in passes the DAW (no devices, no window, no fixed ports). */
struct StageAppOptions {
    bool plugin = false;
    AudioBackendFactory makeBackend;     // empty -> hosted (no devices)
};

// Prints a boot step and appends it to startup_trace.log (survives native crashes).
void bootStep(const char* msg);

class StageApp {
public:
    StageApp(int argc, char* argv[], StageAppOptions options = {});
    ~StageApp();
    StageApp(const StageApp&) = delete;
    StageApp& operator=(const StageApp&) = delete;

    // Opens the UI, runs the HTTP server (blocking) and the MIDI program worker.
    // Returns the process exit code. (Standalone app.)
    int run();

    // ---- v33 (V0) services without a window (the plug-in uses these directly) --------
    /** Starts the program / maintenance workers, the /ws hub and the HTTP server on
     *  `bindAddress`:`port` (port 0 = any free port). Non-blocking. False if it cannot listen. */
    bool startServices(const std::string& bindAddress, int port);
    /** Stops the server, the hub and the workers (idempotent). */
    void stopServices();
    /** The port the server listens on (after startServices). */
    int servicePort() const { return servicePort_; }
    /** URL that opens the console with this session's token (the window / plug-in editor). */
    std::string consoleUrl() const;
    SonusDSP::WorkstationHost& getEngine() { return engine; }
    /** Changes whenever any parameter may have changed (UI, MIDI, program / preset loads). */
    uint64_t stateRevision() { return paramRevCounter.load() + engine.getRegistry().valueRevision(); }
    bool isPlugin() const { return options_.plugin; }
    /** Plug-in state: the current setup (a program entry) and its restore. */
    json captureState();
    void restoreState(const json& state);
    /** The 8 global programs (the DAW's program list). */
    static constexpr int programCount() { return kProgramSlots; }
    std::string programSlotName(int slot);
    int activeProgram() const { return activeProgramSlot; }
    /** Recalls a program off the caller's thread (the program worker loads it). */
    void requestProgram(int slot) { if(slot>=0 && slot<kProgramSlots) requestedMidiProgram.store(slot); }

private:
    // ---- Route registration (src/app/routes/*Routes.cpp) -----------------
    void registerPageRoutes();
    void registerLibraryRoutes();
    void registerProgramRoutes();
    void registerHardwareRoutes();
    void registerPianoRoutes();
    void registerSoundRoutes();
    void registerSynthRoutes();
    void registerDx7Routes();
    void registerEngineRoutes();
    void registerOrganRoutes();
    void registerRealtimeRoutes();
    void registerBrowserRoutes();     // v32.4 sound browser metadata
    void registerMidiRoutingRoutes(); // v33 (M2) MIDI routing per keyboard / channel
    void registerAboutRoutes();       // ACERCA DE: app info and configurable links
    static json defaultMidiRouting();
    /** Rules -> the engine's (source, channel) table. Off the audio thread; cheap (272 stores). */
    void compileMidiRoutes();
    json midiRoutingJson();
    std::string handleRealtimeCommand(const std::string& text, const std::shared_ptr<StateHub::Client>& client);
    /** Live engine state (without params) for /api/state and /ws. Caller holds dataMutex. */
    json liveStateJson();
    void installHttpGuard();
    static bool isAppPage(const std::string& path);
    static bool serveAppIndex(httplib::Response& res);

    // ---- Sample library (StageApp.cpp) -----------------------------------
    static std::string canonicalSamplePath(const std::string& str);
    std::vector<LibraryEntry> registeredEntries(bool includeLegacy);
    json libraryIndexItems(bool force);
    json libraryProgressJson();

    // ---- Programs and control mappings (StageApp.cpp) --------------------
    void ensurePianoSoundRefs();
    // MIDI / OSC mappings (StageApp.cpp, routes/MappingRoutes.cpp). Scopes:
    // SYSTEM in control_mappings.json, MODULE in each module preset, PROGRAM in programs.
    void loadControlMappings();
    bool persistControlMaps();
    /** After any load: mapped parameters' current values, so pickup compares against them. */
    void syncMappingValues();
    /** A user edit (UI, MIDI, OSC) marks the parameter's module as modified (unsaved). */
    void markParamEdited(const std::string& id);
    // ---- v32.4 live / studio safety -------------------------------------------------
    /** The current setup as a program entry (the STORE format). Caller holds dataMutex. */
    json captureProgramJson(int slot, const std::string& name);
    /** Applies a program entry (the RECALL path). Caller holds dataMutex. */
    void applyProgramJson(const json& prog);
    /** Every 500 ms off the audio/MIDI threads: MIDI hot-plug, audio watchdog, autosave, RAM. */
    void maintenanceTick();
    void autosaveSessionIfNeeded(bool force);
    /** Samples referenced by the 8 programs that no longer exist (checked at startup). */
    void validateSetlist();
    /** v34: library folders (LIBRARY roots, registered files, bundled samples) used to find
     *  samples whose saved path no longer exists. Call after any sample-library change. */
    void refreshSampleSearchRoots();
    /** Saved program/layer reference -> existing file, or empty. Control threads only. */
    fs::path resolveProgramSample(const std::string& saved, const std::string& filename) const;
    void loadSystemSettings();
    void saveSystemSettings();
    /** What a global program stores: its own mappings plus the three modules' mappings. */
    json programMappingsJson();
    void restoreProgramMappings(const json& program);
    void registerMappingRoutes();
    static void serveHtmlPage(const std::string& file, httplib::Response& res);

    // ---- LibraryRoutes.cpp ------------------------------------------------
    bool localLibraryRequest(const httplib::Request& r);

    // ---- PianoRoutes.cpp --------------------------------------------------
    void applySoundEq(int layer,const PianoLayerSampleConfig& c);
    void applyNewSampleRoutes(int layer,const PianoLayerSampleConfig& c);
    json capturePianoLayerSound(int layer);
    bool restorePianoLayerSound(int layer,const json& patch);
    bool sourceMatchesSound(const json& sound, const fs::path& path, int sf2Preset);
    const json* findDefaultSoundForSource(const fs::path& source,int preset);
    json loadPresetsFile(const std::string& path);
    bool savePresetsFile(const std::string& path, const json& arr);
    json modulePatch(int module);
    bool restoreModulePatch(int module,const json& patch);
    bool checkPresetName(std::string& name);
    json readPianoPerformances();

    // ---- SoundRoutes.cpp --------------------------------------------------
    int soundModuleIndex(const std::string& name);
    std::string newSoundId();
    json soundSummary(const json& sound);

    // ---- SynthRoutes.cpp --------------------------------------------------
    json readOverrideFile();
    json factoryState();

    // ---- State (declaration order == construction order) -----------------
    WorkstationHost engine;
    MidiOscMapper oscMapper{engine};
    std::string libraryRootDir;

    // v28.1: user-writable library manifest, separate from legacy programs.json.
    const fs::path sampleLibraryFile = stageUserDataDir()/"sample_library.json";
    std::mutex sampleLibraryMutex;
    json sampleLibraryConfig;
    const fs::path relocationFile = stageUserDataDir()/"sample_relocations.json";
    SampleLibraryManager sampleLibraryWorker;
    // Persistent metadata index; changes to registered sources invalidate it.
    const fs::path libraryIndexFile = stageUserDataDir()/"sample_library_index.json";
    std::mutex libraryIndexMutex;
    json indexedItems = json::array();
    std::string indexedSignature;

    std::string statusMsg = "Listo.";
    std::mutex dataMutex;
    int activeProgramSlot = 0;
    std::string currentProgramName = "Program 1";
    bool initializedEditBuffer = false;
    std::atomic<uint64_t> paramRevCounter{1};

    // v28.2: a Sound is an editable DSP snapshot referencing sample assets.
    const fs::path soundsFilePath = stageUserDataDir()/"sounds_v2.json";
    json soundLibrary;
    json selectedSoundRefs;
    const std::string programsFilePath = stageUserFile("programs.json");
    json userPrograms = json::array();

    StageAppOptions options_;
    std::unique_ptr<AudioBackend> hw;

    const std::string tagsFilePath = stageUserFile("sample_tags.json");
    json sampleTags = json::object();

    // HTTP exposure: loopback only unless started with --lan.
    bool httpLanAccess = false;                    // --lan (this session)
    std::string httpBindAddress = "127.0.0.1";
    // ---- v32.4 native window and HTTP access control -------------------------------
    // Window (default): the console runs in a WebView2 window; other clients (browsers,
    // other programs) are refused unless "web access" is on. The window proves itself with
    // a random per-session token (cookie ls_session), so it never depends on that setting.
    enum class UiMode { Window, Browser, Headless };
    UiMode uiMode = UiMode::Window;
    std::string sessionToken;                      // random, new every start
    std::atomic<bool> webAccess{false};            // setting: browsers on this PC may connect
    std::atomic<bool> webAccessForced{false};      // --web / --browser / --headless / no WebView2
    bool lanSetting = false;                       // setting: listen on the network (next start)
    bool lanActive = false;                        // what this session actually binds
    NativeWindowState windowState;
    bool webAllowed() const { return webAccess.load() || webAccessForced.load(); }

    // Global program bank size (2 rows × 4 pads).
    static constexpr int kProgramSlots = 8;

    // Performance actions are queued on the MIDI callback, not on the audio thread.
    std::atomic<int> requestedMidiProgram{-1};
    // v32.4 session autosave (studio): the edit buffer is written every 30 s while it
    // changes; a lock file tells a crash from a clean exit, so the next start can offer it.
    std::atomic<uint64_t> editEpoch{0};
    uint64_t autosaveRev = 0, autosaveEpoch = 0;
    std::chrono::steady_clock::time_point lastAutosave{};
    std::filesystem::path sessionDir;
    json sessionRecovery = nullptr;            // {savedAt, programName} when restorable
    json missingSamples = json::array();       // [{slot, name, file}]
    std::atomic<uint64_t> sampleMemoryBudgetMB{4096};   // A5: unused instruments above this are released
    const std::string mappingsFilePath = stageUserFile("control_mappings.json");
    std::string uiRootDir;
    std::atomic<uint64_t> lastSavedMappingRevision{0};
    // Unsaved edits per module (0 piano, 1 synth, 2 organ): set by user edits and
    // module-mapping changes, cleared when the module is loaded or saved.
    std::array<std::atomic<bool>,3> moduleDirty{};
    // Unsaved edits per piano layer Sound (sample settings, inserts, sends, Sound mappings).
    std::array<std::atomic<bool>,PIANO_LAYER_COUNT> soundDirty{};
    /** A Sound's mappings written for another layer, rewritten for `layer`. */
    json retargetSoundMappings(const json& mappings, int layer);
    void clearAllSoundDirty(){ for(auto& d:soundDirty)d.store(false); }
    // v35: per-layer "edited away from its Sound" flags travel with Performances and
    // Programs, so a recalled variation never poses as the untouched Sound.
    json soundEditedJson() const {
        json a=json::array(); for(const auto& d:soundDirty)a.push_back(d.load()); return a;
    }
    void restoreSoundEdited(const json& flags){
        for(size_t i=0;i<soundDirty.size();++i)
            soundDirty[i].store(flags.is_array()&&i<flags.size()&&flags[i].is_boolean()&&flags[i].get<bool>());
    }
    // v35 one-time data repair (StageApp.cpp): Sounds no longer remember a layer, and
    // Program / Performance Sound references are re-validated against their samples.
    void migrateSoundsV35();
    // v35: removes shimmer slots stored by the retired v1–v4 engines from the user's
    // Programs, Performances and module presets (backup "<file>.before-shimmer-v5.bak").
    void retireOldShimmersV35();

    // V17 modular presets: only the selected module and its own Studio rack.
    const std::string pianoPresetsPath = stageUserPresetFile("piano");
    const std::string pianoPerformancesPath = (stageUserDataDir()/"piano_performances.json").string();
    const std::string factoryOverridesPath = (stageUserDataDir()/"presets/synth/user/factory_overrides.json").string();
    // Factory patches: explicit runtime/version diagnostics for the browser.
    static constexpr int kFactoryPresetCount = SonusDSP::SYNTH_FACTORY_PRESET_COUNT;
    const std::string factoryBuild = "SONUS 32.3 / Factory API 10 (184 JSON factory files: 96 designs + 48 vintage + 24 DX7 + 16 LA32; mod matrix, 303 sequencer)";
    // Dedicated synth / organ presets (unified, no duplicates).
    const std::string synthPresetsPath = stageUserPresetFile("synth");
    const std::string organPresetsPath = stageUserPresetFile("organ");

    // Real-time publisher for /ws (started in run(), stopped before teardown).
    std::unique_ptr<StateHub> hub;

    // v33 (M2) MIDI routing rules (system setting), guarded by routingMutex_.
    std::mutex routingMutex_;
    json midiRouting_ = defaultMidiRouting();   // {enabled, pedalFollows, rules}

    // v33 services (startServices / stopServices).
    std::atomic<bool> servicesRunning_{false};
    std::atomic<bool> listenOk_{true};
    std::atomic<bool> httpStopping_{false};        // set before svr.stop(): long-lived handlers (/ws) leave
    int servicePort_ = 8080;
    std::thread programWorker_, maintenanceWorker_, serverThread_;

    httplib::Server svr;
};
