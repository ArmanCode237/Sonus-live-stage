#include "app/SampleSources.h"
#include "core/LibraryPathRelocation.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <unordered_map>
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
    if(requested.empty())return requested;
    if(fs::is_regular_file(requested,ec))return requested;
    {
        std::lock_guard<std::mutex> guard(relocatedPathsMutex);
        for(auto it=relocatedPaths.rbegin();it!=relocatedPaths.rend();++it){
            std::string target;
            if(relocateSourcePath(requested.generic_string(),it->first,it->second,target))
                return fs::path(target);
        }
    }
    if(auto found=findSampleInSearchRoots(requested.string());!found.empty())return found;
    return requested;
}

// ---------------------------------------------------------------- v34 sample search roots
namespace {
std::mutex searchMutex;
std::vector<fs::path> searchRoots;
std::unordered_map<std::string,fs::path> nameIndex;   // lower-case file name -> first match
bool nameIndexBuilt=false;
std::chrono::steady_clock::time_point nameIndexTime{};

std::string lowerCopy(std::string s){
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});
    return s;
}
bool isSampleExtension(const fs::path& p){
    const auto ext=lowerCopy(p.extension().string());
    return ext==".npno"||ext==".nsp"||ext==".nsmp"||ext==".nsmp3"||ext==".nsmp4"||ext==".sf2"||ext==".sfz";
}
// Caller holds searchMutex. Bounded walk: libraries are large but shallow.
void buildNameIndex(){
    nameIndex.clear();
    size_t visited=0;
    for(const auto& root:searchRoots){
        std::error_code ec;
        fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied,ec),end;
        for(;!ec&&it!=end&&visited<200000;it.increment(ec)){
            ++visited;
            if(it.depth()>8){it.disable_recursion_pending();continue;}
            std::error_code fileEc;
            if(!it->is_regular_file(fileEc)||!isSampleExtension(it->path()))continue;
            nameIndex.emplace(lowerCopy(it->path().filename().string()),it->path());
        }
    }
    nameIndexBuilt=true;
    nameIndexTime=std::chrono::steady_clock::now();
}
} // namespace

void setSampleSearchRoots(const std::vector<fs::path>& roots){
    std::vector<fs::path> unique;
    for(const auto& r:roots){
        std::error_code ec;
        if(r.empty()||!fs::is_directory(r,ec))continue;
        auto canonical=fs::weakly_canonical(r,ec);
        if(ec)canonical=r.lexically_normal();
        if(std::find(unique.begin(),unique.end(),canonical)==unique.end())unique.push_back(canonical);
    }
    std::lock_guard<std::mutex> guard(searchMutex);
    if(unique==searchRoots)return;
    searchRoots=std::move(unique);
    nameIndex.clear();
    nameIndexBuilt=false;
}

fs::path findSampleInSearchRoots(const std::string& saved){
    // Programs saved on Windows keep backslashes; normalise before splitting.
    std::string normalised=saved;
    std::replace(normalised.begin(),normalised.end(),'\\','/');
    const fs::path path(normalised);
    const fs::path filename=path.filename();
    if(filename.empty())return {};
    // Relative tails to try under each root, most specific first:
    //   ./samples/piano-ep/X.npno -> piano-ep/X.npno ; C:/Lib/piano-ep/X.npno -> piano-ep/X.npno ; X.npno
    std::vector<fs::path> tails;
    {
        std::vector<fs::path> parts;
        for(const auto& part:path)if(!part.empty()&&part!="/"&&part!="."&&part.string().find(':')==std::string::npos)parts.push_back(part);
        for(size_t i=0;i+1<parts.size();++i){
            if(lowerCopy(parts[i].string())=="samples"){
                fs::path tail;for(size_t j=i+1;j<parts.size();++j)tail/=parts[j];
                tails.push_back(tail);break;
            }
        }
        if(parts.size()>=2)tails.push_back(parts[parts.size()-2]/filename);
        tails.push_back(filename);
    }
    std::lock_guard<std::mutex> guard(searchMutex);
    for(const auto& root:searchRoots)for(const auto& tail:tails){
        std::error_code ec;
        const auto candidate=root/tail;
        if(fs::is_regular_file(candidate,ec))return candidate;
    }
    // Same file name anywhere inside the roots (a library reorganised into subfolders).
    const auto key=lowerCopy(filename.string());
    const auto lookup=[&]()->fs::path{
        const auto it=nameIndex.find(key);
        std::error_code ec;
        return it!=nameIndex.end()&&fs::is_regular_file(it->second,ec)?it->second:fs::path{};
    };
    if(!nameIndexBuilt)buildNameIndex();
    if(auto hit=lookup();!hit.empty())return hit;
    // Files copied in after the index was built: refresh at most every 15 s on a miss.
    if(std::chrono::steady_clock::now()-nameIndexTime>std::chrono::seconds(15)){
        buildNameIndex();
        return lookup();
    }
    return {};
}

// The piano-layer source paths are stored separately from the realtime DSP parameters.
// This keeps legacy "sourcePath" (layer 1) compatible with older programs.json files.
json serializePianoLayerSources(const StagePianoEngine& engine) {
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

bool restorePianoLayerSources(const json& program, StagePianoEngine& engine,
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
            else if (auto found = findSampleInSearchRoots(filename); !found.empty()) resolved = found;
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
