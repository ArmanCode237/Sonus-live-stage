#pragma once
#include "../core/NordTypes.h"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <unordered_map>
#include <vector>

namespace NordDSP {

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

private:
    struct Entry {
        std::shared_ptr<PianoInstrument> instrument;
        std::filesystem::file_time_type mtime{};
        uintmax_t size = 0;
        bool loading = false;
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

} // namespace NordDSP
