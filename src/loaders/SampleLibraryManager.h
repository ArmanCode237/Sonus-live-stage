#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace SonusDSP {
// One bounded background worker prepares disk caches without retaining decoded
// instruments in RAM. Control-thread only; never called by the audio callback.
class SampleLibraryManager {
public:
    struct Progress { bool running=false; bool cancelled=false; uint64_t total=0,done=0,failed=0,skipped=0; std::string current,error; };
    ~SampleLibraryManager();
    bool prepareAsync(std::vector<std::string> paths);
    void cancel();
    Progress progress() const;
private:
    mutable std::mutex mutex_;
    std::mutex launchMutex_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    Progress status_;
};
}
