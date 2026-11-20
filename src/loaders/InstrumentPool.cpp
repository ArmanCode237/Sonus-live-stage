#include "InstrumentPool.h"
#include "BankLoader.h"
#include "Sf2Loader.h"
#include "DecodedSampleCache.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>

namespace fs=std::filesystem;
namespace SonusDSP {

InstrumentPool::~InstrumentPool(){
    if(preloadThread_.joinable())preloadThread_.join();
    { std::lock_guard<std::mutex> lock(cacheMutex_); stopCache_ = true; }
    cacheCv_.notify_all();
    if(cacheThread_.joinable())cacheThread_.join();
}

void InstrumentPool::enqueueCache(CacheJob job) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        // Only one disk writer is active, even if the setlist preloads many
        // large instruments. Never spin up detached jobs per instrument.
        pendingCache_.push_back(std::move(job));
        if(!cacheThread_.joinable())
            cacheThread_ = std::thread([this]{cacheWriterLoop();});
    }
    cacheCv_.notify_one();
}

void InstrumentPool::cacheWriterLoop() {
    for (;;) {
        CacheJob job;
        {
            std::unique_lock<std::mutex> lock(cacheMutex_);
            cacheCv_.wait(lock,[this]{return stopCache_ || !pendingCache_.empty();});
            if(pendingCache_.empty() && stopCache_)return;
            job=std::move(pendingCache_.front());
            pendingCache_.pop_front();
        }
        // A user may replace the source while a cache job is queued. Do not
        // write old audio under the new source stamp (false cache hit).
        std::error_code ec;
        const auto newSize=fs::file_size(job.path,ec);
        if(ec || newSize!=job.sourceSize)continue;
        const auto newTime=fs::last_write_time(job.path,ec);
        if(ec || newTime!=job.sourceTime)continue;
        try {
            if(fs::path(job.path).extension()==".npno" || fs::path(job.path).extension()==".nsp")
                BankLoader::persistNpnoCache(job.path,*job.instrument);
            else DecodedSampleCache::store(job.path,job.sf2Preset,*job.instrument);
        } catch (const std::exception& ex) {
            std::cerr<<"[CNSP Cache] No se pudo persistir: "<<ex.what()<<"\n";
        } catch (...) {
            std::cerr<<"[CNSP Cache] Error inesperado en escritura.\n";
        }
    }
}


std::shared_ptr<PianoInstrument> InstrumentPool::getOrLoad(const std::string& path,
       std::string& outStatusMsg,int sf2Preset){
    std::error_code ec;
    fs::path canonical=fs::weakly_canonical(fs::path(path),ec);
    if(ec || !fs::exists(canonical,ec)){
        outStatusMsg="No se encontró el instrumento: "+path;return {};
    }
    const bool regular=fs::is_regular_file(canonical,ec);
    const uintmax_t bytes=regular?fs::file_size(canonical,ec):0;
    if(ec){outStatusMsg="Error obteniendo tamaño del instrumento";return {};}
    const auto modified=fs::last_write_time(canonical,ec);
    if(ec){outStatusMsg="Error obteniendo fecha del instrumento";return {};}
    std::string ext=canonical.extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    // Do not collapse distinct SoundFont presets into one cached instrument.
    const std::string key=canonical.generic_string()+"#sf2:"+std::to_string(sf2Preset);
    std::shared_ptr<Entry> entry;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto& slot=entries_[key];
        if(!slot)slot=std::make_shared<Entry>();
        entry=slot;
        while(entry->loading)entry->cv.wait(lock);
        entry->lastUse=std::chrono::steady_clock::now();
        if(entry->instrument && entry->mtime==modified && entry->size==bytes){
            outStatusMsg="Instrumento en RAM: "+entry->instrument->name;
            return entry->instrument;
        }
        entry->loading=true;
        entry->instrument.reset();
    }
    auto candidate=std::make_shared<PianoInstrument>();
    bool loaded=false;
    try {
        if(DecodedSampleCache::supports(canonical.string()) &&
           DecodedSampleCache::load(canonical.string(),sf2Preset,*candidate)) {
            loaded=true;outStatusMsg="Desde caché SSD PCM16: "+candidate->name;
        }
        else if(sf2Preset>=0 && ext==".sf2")
            loaded=Sf2Loader::load(canonical.string(),*candidate,outStatusMsg,sf2Preset);
        else if(ext==".npno" || ext==".nsp") {
            // Fully decode and validate PCM before publishing. The potentially
            // expensive cache write is queued on a separate control worker.
            loaded=BankLoader::loadNpnoNative(canonical.string(),*candidate,
                                               outStatusMsg,true);
        }
        else
            loaded=BankLoader::loadAny(canonical.string(),*candidate,outStatusMsg);
    }catch(const std::exception& e){outStatusMsg=std::string("Error decodificando instrumento: ")+e.what();}
    catch(...){outStatusMsg="Error desconocido en decodificador de instrumento";}
    const bool fallbackNsmp=ext.rfind(".nsmp",0)==0 &&
        outStatusMsg.rfind("Sample Synth cargado:",0)==0;
    const bool needsCacheWrite=loaded && !fallbackNsmp &&
        ((ext==".npno"||ext==".nsp") || DecodedSampleCache::supports(canonical.string())) &&
        outStatusMsg.rfind("Desde caché PCM16:",0)!=0 &&
        outStatusMsg.rfind("Desde caché SSD PCM16:",0)!=0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(loaded){
            // Never mark an incomplete or malformed NPNo instrument as ready.
            if(ext==".npno"){
                for(const auto& stroke:candidate->strokes){
                    if(!stroke.readyFrames || stroke.readyFrames->load(std::memory_order_acquire)!=stroke.frames){
                        loaded=false;
                        outStatusMsg="Banco CNSP incompleto: " + canonical.filename().string();
                        break;
                    }
                }
            }
        }
        if(loaded){
            entry->instrument=candidate;
            entry->mtime=modified;entry->size=bytes;
        }
        entry->loading=false;
    }
    entry->cv.notify_all();
    if(loaded && needsCacheWrite){
        // Failure to start a cache writer must never make an otherwise
        // playable instrument fail to load.
        try { enqueueCache({canonical.string(),candidate,bytes,modified,sf2Preset}); }
        catch(const std::exception& e){
            std::cerr<<"[CNSP Cache] Escritor no disponible: "<<e.what()<<"\n";
        }
    }
    return loaded?candidate:std::shared_ptr<PianoInstrument>{};
}

void InstrumentPool::preloadSetlistAsync(const std::vector<std::string>& paths){
    std::vector<std::pair<std::string,int>> expanded;
    expanded.reserve(paths.size());
    for(const auto& path:paths)expanded.emplace_back(path,-1);
    preloadSetlistAsync(expanded);
}

void InstrumentPool::preloadSetlistAsync(
      const std::vector<std::pair<std::string,int>>& sources){
    if(preloadThread_.joinable())preloadThread_.join();
    preloadThread_=std::thread([this,sources]{
        for(const auto& source:sources){
            if(source.first.empty())continue;
            std::string status;
            auto bank=getOrLoad(source.first,status,source.second);
            {   // setlist instruments stay in RAM for instant program changes
                std::lock_guard<std::mutex> lock(mutex_);
                for(auto& [key,e]:entries_) if(e && e->instrument==bank && bank) e->pinned=true;
            }
            // Avoid decompressing eight large banks simultaneously. Do not
            // block API callbacks: this wait runs only on the prefetch worker.
            if(bank){
                for(unsigned attempts=0;attempts<10000;++attempts){
                    bool complete=true;
                    for(const auto& stroke:bank->strokes){
                        if(stroke.readyFrames &&
                           stroke.readyFrames->load(std::memory_order_acquire)<stroke.frames){
                            complete=false;break;
                        }
                    }
                    if(complete)break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        }
    });
}

double InstrumentPool::residentMB(){
    std::lock_guard<std::mutex> lock(mutex_);
    double total=0;
    for(const auto& [key,e]:entries_) if(e && e->instrument) total+=e->instrument->memoryMB;
    return total;
}

double InstrumentPool::trimUnused(uint64_t budgetMB){
    std::lock_guard<std::mutex> lock(mutex_);
    double total=0;
    for(const auto& [key,e]:entries_) if(e && e->instrument) total+=e->instrument->memoryMB;
    if(total<=double(budgetMB)) return 0;
    // Candidates: loaded, not pinned, not loading, and referenced ONLY by the pool
    // (use_count 1: no layer, sounding voice, retired list or cache writer holds it).
    std::vector<std::pair<std::chrono::steady_clock::time_point,std::string>> lru;
    for(const auto& [key,e]:entries_)
        if(e && e->instrument && !e->pinned && !e->loading && e->instrument.use_count()==1) lru.emplace_back(e->lastUse,key);
    std::sort(lru.begin(),lru.end());
    double released=0;
    for(const auto& [when,key]:lru){
        if(total<=double(budgetMB)) break;
        auto it=entries_.find(key);
        if(it==entries_.end() || !it->second || !it->second->instrument) continue;
        const double mb=it->second->instrument->memoryMB;
        std::cout<<"[RAM] Liberando "<<it->second->instrument->name<<" ("<<int(mb)<<" MB, sin uso)\n";
        it->second->instrument.reset();
        entries_.erase(it);
        total-=mb; released+=mb;
    }
    return released;
}

} // namespace SonusDSP
