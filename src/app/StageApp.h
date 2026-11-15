#pragma once
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
#include <vector>

class HardwareManager;

// Prints a boot step and appends it to startup_trace.log (survives native crashes).
void bootStep(const char* msg);

class StageApp {
public:
    StageApp(int argc, char* argv[]);
    ~StageApp();
    StageApp(const StageApp&) = delete;
    StageApp& operator=(const StageApp&) = delete;

    // Opens the UI, runs the HTTP server (blocking) and the MIDI program worker.
    // Returns the process exit code.
    int run();

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

    std::unique_ptr<HardwareManager> hw;

    const std::string tagsFilePath = stageUserFile("sample_tags.json");
    json sampleTags = json::object();

    // HTTP exposure: loopback only unless started with --lan.
    bool httpLanAccess = false;
    std::string httpBindAddress = "127.0.0.1";

    // Global program bank size (2 rows × 4 pads).
    static constexpr int kProgramSlots = 8;

    // Performance actions are queued on the MIDI callback, not on the audio thread.
    std::atomic<int> requestedMidiProgram{-1};
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

    // V17 modular presets: only the selected module and its own Studio rack.
    const std::string pianoPresetsPath = stageUserPresetFile("piano");
    const std::string pianoPerformancesPath = (stageUserDataDir()/"piano_performances.json").string();
    const std::string factoryOverridesPath = (stageUserDataDir()/"presets/synth/user/factory_overrides.json").string();
    // Factory patches: explicit runtime/version diagnostics for the browser.
    static constexpr int kFactoryPresetCount = NordDSP::SYNTH_FACTORY_PRESET_COUNT;
    const std::string factoryBuild = "SONUS 32.3 / Factory API 10 (184 JSON factory files: 96 designs + 48 vintage + 24 DX7 + 16 LA32; mod matrix, 303 sequencer)";
    // Dedicated synth / organ presets (unified, no duplicates).
    const std::string synthPresetsPath = stageUserPresetFile("synth");
    const std::string organPresetsPath = stageUserPresetFile("organ");

    // Real-time publisher for /ws (started in run(), stopped before teardown).
    std::unique_ptr<StateHub> hub;

    httplib::Server svr;
};
