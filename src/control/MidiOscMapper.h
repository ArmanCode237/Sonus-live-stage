#pragma once
#include "../core/WorkstationHost.h"
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>

namespace NordDSP {

class MidiOscMapper {
public:
    explicit MidiOscMapper(WorkstationHost& host);
    ~MidiOscMapper();

    bool startOscServer(int udpPort = 9000);
    void stopOscServer();

    int getOscPort() const { return oscPort_.load(); }
    bool isOscRunning() const { return oscRunning_.load(); }
    std::string getLastOscAddress() const;
    // Mapped OSC addresses live in ControlMappings (scopes, learn). Unmapped
    // "/param/<id>" or "/<id>" messages still write that parameter in raw units.

private:
    void oscThreadLoop(int port);
    void parseOscPacket(const char* buf, int len);

    WorkstationHost& host_;
    std::atomic<bool> oscRunning_{false};
    std::atomic<int> oscPort_{9000};
    std::thread oscThread_;
    mutable std::mutex oscMutex_;
    std::string lastOscAddress_ = "None";
};

} // namespace NordDSP