#include "SampleLibraryManager.h"
#include "DecodedSampleCache.h"
#include "BankLoader.h"
#include "NsmpLoader.h"
#include "Sf2Loader.h"
#include <algorithm>
#include <filesystem>
#include <set>
namespace fs=std::filesystem;
namespace SonusDSP {
SampleLibraryManager::~SampleLibraryManager(){cancel_.store(true);if(worker_.joinable())worker_.join();}
void SampleLibraryManager::cancel(){cancel_.store(true,std::memory_order_release);}
SampleLibraryManager::Progress SampleLibraryManager::progress()const{std::lock_guard<std::mutex> g(mutex_);return status_;}
bool SampleLibraryManager::prepareAsync(std::vector<std::string> paths){
    std::lock_guard<std::mutex> exclusiveLaunch(launchMutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(status_.running)return false;
    }
    if(worker_.joinable())worker_.join();
    std::set<std::string> unique;
    for(const auto& path:paths){
        std::error_code ec;const auto canonical=fs::weakly_canonical(path,ec);
        if(!ec&&fs::is_regular_file(canonical,ec) && !ec)unique.insert(canonical.generic_string());
    }
    if(unique.empty())return false;
    {std::lock_guard<std::mutex> lock(mutex_);status_={};status_.running=true;status_.total=unique.size();}
    cancel_.store(false,std::memory_order_release);
    worker_=std::thread([this,sources=std::move(unique)](){
        for(const auto& source:sources){
            if(cancel_.load(std::memory_order_acquire))break;
            {std::lock_guard<std::mutex> g(mutex_);status_.current=source;}
            auto ext=fs::path(source).extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
            bool success=false,skip=false;
            std::string report;
            try{
                if(ext==".npno"||ext==".nsp"){
                    if(BankLoader::hasNpnoCache(source)){success=true;skip=true;}
                    else{
                        PianoInstrument bank;
                        success=BankLoader::loadNpnoNative(source,bank,report,false);
                        // A hit in v27's legacy .cache is readable, but it must
                        // be copied into the configured SSD cache directory.
                        if(success&&!BankLoader::hasNpnoCache(source))
                            BankLoader::persistNpnoCache(source,bank);
                        success=success&&BankLoader::hasNpnoCache(source);
                    }
                } else if(DecodedSampleCache::supports(source)){
                    if(DecodedSampleCache::containsFresh(source)){success=true;skip=true;}
                    else{
                        PianoInstrument bank;
                        if(ext==".sf2")success=Sf2Loader::load(source,bank,report,-1);
                        else if(ext==".sfz")success=BankLoader::loadFromSfz(source,bank);
                        else success=NsmpLoader::loadNsmpNative(source,bank,report);
                        if(success)success=DecodedSampleCache::store(source,-1,bank);
                    }
                } else{success=false;report="Formato sin caché PCM; los presets de programa no contienen audio decodificable.";}
            }catch(const std::exception& e){success=false;report=e.what();}
            catch(...){success=false;report="Error desconocido preparando el instrumento.";}
            {std::lock_guard<std::mutex> g(mutex_);
                ++status_.done;if(skip)++status_.skipped;
                if(!success){++status_.failed;status_.error=source+": "+report;}
            }
        }
        {std::lock_guard<std::mutex> g(mutex_);
            status_.cancelled=cancel_.load(std::memory_order_acquire);
            status_.running=false;status_.current.clear();
        }
    });
    return true;
}
}
