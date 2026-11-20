#include "app/UserDataPaths.h"
#include <filesystem>
namespace fs = std::filesystem;
#include <cstdlib>


// Per-user writable storage: Program Files and app bundles are not writable.
std::filesystem::path stageUserDataDir(){
    namespace fs=std::filesystem;
#if defined(_WIN32)
    const char* base=std::getenv("LOCALAPPDATA");
    fs::path dir=base?fs::path(base)/"Live Stage":fs::current_path()/"UserData";
#elif defined(__APPLE__)
    const char* home=std::getenv("HOME");
    fs::path dir=home?fs::path(home)/"Library/Application Support/Live Stage":fs::current_path()/"UserData";
#else
    const char* xdg=std::getenv("XDG_DATA_HOME");
    const char* home=std::getenv("HOME");
    fs::path dir=xdg?fs::path(xdg)/"live-stage":(home?fs::path(home)/".local/share/live-stage":fs::current_path()/"UserData");
#endif
    std::error_code ec;fs::create_directories(dir,ec);
    return dir;
}
static std::string& stageAssetRoot() { static std::string root; return root; }
void setStageAssetRoot(const std::string& root) { stageAssetRoot() = root; }

std::string stageUserFile(const char* name){
    const auto path=stageUserDataDir()/name;
    if(!std::filesystem::exists(path)) {
        const auto source = (!stageAssetRoot().empty() &&
                             std::filesystem::is_regular_file(std::filesystem::path(stageAssetRoot())/name))
                            ? std::filesystem::path(stageAssetRoot())/name
                            : std::filesystem::path(name);
        if(std::filesystem::is_regular_file(source)){
            std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);
            std::filesystem::copy_file(source,path,std::filesystem::copy_options::skip_existing,ec);
        }
    }
    return path.string();
}

std::string stageUserPresetFile(const char* module){
    namespace fs=std::filesystem;
    const std::string sub=std::string("presets/")+module+"/user/presets.json";
    const auto file=stageUserDataDir()/sub;
    std::error_code ec;fs::create_directories(file.parent_path(),ec);
    if(!fs::exists(file)){
        const std::string legacy=std::string(module)+"_presets.json";
        const fs::path bundle=fs::path(stageAssetRoot())/sub;
        const fs::path source=fs::exists(legacy)?fs::path(legacy):
                              fs::is_regular_file(bundle)?bundle:fs::path(sub);
        if(fs::is_regular_file(source))fs::copy_file(source,file,ec);
    }
    return file.string();
}
std::string findUiDir() {
    // Locate shipped UI/preset assets even when launched from build/Release.
    std::error_code ec;
    if(!stageAssetRoot().empty()){
        const auto bundled=fs::path(stageAssetRoot())/"ui";
        if(fs::is_directory(bundled,ec))return bundled.generic_string();
    }
    auto probe=fs::current_path(ec);
    if(!ec)for(int depth=0;depth<7;++depth){
        for(const auto& sub:{"ui","Resources/ui","resources/ui"}){
            const auto candidate=probe/sub;
            if(fs::is_directory(candidate,ec))return candidate.generic_string();
        }
        if(!probe.has_parent_path()||probe==probe.parent_path())break;
        probe=probe.parent_path();
    }
    return "ui";
}

std::string findUiFile(const std::string& filename) {
    return (fs::path(findUiDir()) / filename).generic_string();
}
