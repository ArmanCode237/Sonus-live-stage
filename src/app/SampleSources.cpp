#include "app/SampleSources.h"
#include "core/LibraryPathRelocation.h"
#include <fstream>
#if defined(_WIN32)
#include <windows.h>
#endif

// Shared PCM16 pool for the eight global program slots and five piano layers.
InstrumentPool instPool;

// Never replace an existing program bank with a partial/truncated JSON file.
bool saveJsonAtomic(const fs::path& destination,const json& value){
    const fs::path temporary=destination.string()+".tmp";
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
     if(!out)return false;
     out<<value.dump(2);out.flush();if(!out.good())return false;
    }
#if defined(_WIN32)
    const bool ok=MoveFileExW(temporary.wstring().c_str(),destination.wstring().c_str(),
                              MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::error_code ec;fs::rename(temporary,destination,ec);const bool ok=!ec;
#endif
    if(!ok){std::error_code ec;fs::remove(temporary,ec);}return ok;
}

// v28.2: Stable source aliases recorded when a user moves a library folder.
// Only HTTP/control threads access this table; never the audio callback.
std::mutex relocatedPathsMutex;
std::vector<std::pair<fs::path,fs::path>> relocatedPaths;
fs::path resolveSavedSamplePath(const fs::path& requested){
    std::error_code ec;
    if(fs::is_regular_file(requested,ec))return requested;
    std::lock_guard<std::mutex> guard(relocatedPathsMutex);
    for(auto it=relocatedPaths.rbegin();it!=relocatedPaths.rend();++it){
        std::string target;
        if(relocateSourcePath(requested.generic_string(),it->first,it->second,target))
            return fs::path(target);
    }
    return requested;
}

// The piano-layer source paths are stored separately from the realtime DSP parameters.
// This keeps legacy "sourcePath" (layer 1) compatible with older programs.json files.
json serializePianoLayerSources(const NordPianoEngine& engine) {
    json layers = json::array();
    const auto banks = engine.getPianoInstruments();
    for (int i = 0; i < PIANO_LAYER_COUNT; ++i) {
        auto bank = banks[i];
        const std::string path = bank ? resolveSavedSamplePath(fs::path(bank->sourcePath)).generic_string() : "";
        layers.push_back({{"layer", i}, {"name", bank ? bank->name : "Sin sonido"},
                          {"sourcePath", path},
                          {"fileName", path.empty() ? "" : fs::path(path).filename().string()},
                          {"instrumentId", bank ? bank->id : 0},
                          {"sf2Preset",bank ? bank->sf2PresetIndex : -1}});
    }
    return layers;
}

bool restorePianoLayerSources(const json& program, NordPianoEngine& engine,
                              const std::string& libraryRoot, std::string& statusMsg) {
    bool loadedFirst = false;
    const json layers = (program.contains("pianoLayers") && program["pianoLayers"].is_array())
        ? program["pianoLayers"] : json::array();
    for (int i = 0; i < PIANO_LAYER_COUNT; ++i) {
        const json layer = (i < static_cast<int>(layers.size()) && layers[i].is_object())
            ? layers[i] : json::object();
        const std::string savedPath = i == 0 && layer.empty()
            ? program.value("sourcePath", std::string{}) : layer.value("sourcePath", std::string{});
        const std::string filename = i == 0 && layer.empty()
            ? program.value("fileName", std::string{}) : layer.value("fileName", std::string{});
        fs::path resolved=resolveSavedSamplePath(fs::path(savedPath));
        if ((!fs::exists(resolved) || savedPath.empty()) && !filename.empty()) {
            const fs::path replacement = fs::path(libraryRoot) / filename;
            if (fs::exists(replacement)) resolved = replacement;
        }
        if (savedPath.empty() && filename.empty()) {
            engine.setLayerInstrument(i, {});
            continue;
        }
        if (!fs::exists(resolved) || !fs::is_regular_file(resolved)) {
            statusMsg = "No se encontró el sample de la capa " + std::to_string(i + 1) + ": " + filename;
            engine.setLayerInstrument(i, {}); // prevent a different program's layer from leaking in
            continue;
        }
        const int sf2Preset=layer.value("sf2Preset",-1);
        auto oldBank = engine.getLayerInstrument(i);
        if (oldBank && fs::path(oldBank->sourcePath) == resolved && oldBank->sf2PresetIndex==sf2Preset) {
            if (i == 0) loadedFirst = true;
            continue;
        }
        auto bank = instPool.getOrLoad(resolved.generic_string(), statusMsg,sf2Preset);
        if (bank) {
            engine.setLayerInstrument(i, bank);
            if (i == 0) loadedFirst = true;
        } else {
            engine.setLayerInstrument(i, {});
        }
    }
    return loadedFirst;
}
