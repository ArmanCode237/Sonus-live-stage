#pragma once
#include "../core/WorkstationHost.h"
#include <string>
#include <thread>
#include <atomic>

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