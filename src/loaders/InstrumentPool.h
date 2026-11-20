#pragma once
#include "../core/StageTypes.h"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <unordered_map>
#include <vector>

namespace SonusDSP {

// Thread-safe control-thread cache. Call getOrLoad only from HTTP/control workers;
// the real-time render() uses immutable shared_ptr snapshots, never this mutex.
class InstrumentPool {
public:
    InstrumentPool() = default;
    ~InstrumentPool();
    InstrumentPool(const InstrumentPool&) = delete;
    InstrumentPool& operator=(const InstrumentPool&) = delete;

    std::shared_ptr<PianoInstrument> getOrLoad(const std::string& path,
                                               std::string& outStatusMsg,
                                               int sf2Preset = -1);
    void preloadSetlistAsync(const std::vector<std::string>& paths);
    // Preserves SF2 preset bank/program selection in setlist warmup.
    void preloadSetlistAsync(const std::vector<std::pair<std::string,int>>& sources);
    /** v32.4 sample-RAM budget: when the instruments in RAM exceed `budgetMB`, release the
     *  least recently used ones that nothing else holds (no layer, voice or cache writer)
     *  and that are not part of the setlist. Returns the MB released. Control thread only. */
    double trimUnused(uint64_t budgetMB);
    /** MB of decoded samples currently held by the pool. */
    double residentMB();

private:
    struct Entry {
        std::shared_ptr<PianoInstrument> instrument;
        std::filesystem::file_time_type mtime{};
        uintmax_t size = 0;
        bool loading = false;
        bool pinned = false;                                   // setlist: never released
        std::chrono::steady_clock::time_point lastUse{};
        std::condition_variable cv;
    };
    std::unordered_map<std::string,std::shared_ptr<Entry>> entries_;
    std::mutex mutex_;
    std::thread preloadThread_;
    // Single low-priority-purpose writer keeps cache disk I/O off the request
    // path. Destruction flushes outstanding jobs without detaching threads.
    struct CacheJob {
        std::string path;
        std::shared_ptr<PianoInstrument> instrument;
        uintmax_t sourceSize=0;
        std::filesystem::file_time_type sourceTime{};
        int sf2Preset=-1;
    };
    std::deque<CacheJob> pendingCache_;
    std::mutex cacheMutex_;
    std::condition_variable cacheCv_;
    std::thread cacheThread_;
    bool stopCache_ = false;
    void enqueueCache(CacheJob job);
    void cacheWriterLoop();
};

} // namespace SonusDSP
