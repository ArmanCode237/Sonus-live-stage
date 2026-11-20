#pragma once
#include "../core/WorkstationHost.h"
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <nlohmann/json.hpp>

namespace SonusDSP {

/** v32.4 OSC "device" (like REAPER's OSC control surface): the remote controller's IP and
 *  port for feedback, and whether only that IP may send to Live Stage. */
struct OscDeviceConfig {
    std::string deviceIp;            // empty: no feedback target
    int devicePort = 9000;
    bool feedback = false;           // send mapped parameter values back to the device
    bool onlyFromDevice = false;     // ignore OSC from any other IP (security on shared networks)
};

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

    // ---- v32.4 device / feedback
    void setDevice(const std::string& ip, int port, bool feedback, bool onlyFromDevice);
    OscDeviceConfig deviceConfig() const;
    /** Sends one OSC message (float) to the device. False without a valid device IP. */
    bool sendToDevice(const std::string& address, float value);
    /** Status for the UI: port, this PC's IPv4 addresses, device, last message and sender. */
    nlohmann::json status() const;
    /** IPv4 addresses of this computer (to type into the remote controller). */
    static std::vector<std::string> localAddresses();

private:
    void oscThreadLoop(int port);
    void feedbackLoop();
    void handlePacket(const char* buf, size_t len, int depth);
    void handleMessage(const char* buf, size_t len);

    WorkstationHost& host_;
    std::atomic<bool> oscRunning_{false};
    std::atomic<int> oscPort_{9000};
    std::thread oscThread_;
    mutable std::mutex oscMutex_;
    std::string lastOscAddress_ = "None";
    std::string lastSender_;
    uint64_t received_ = 0, rejected_ = 0, sent_ = 0;

    OscDeviceConfig device_;                       // oscMutex_
    std::atomic<uint32_t> deviceAddr_{0};          // IPv4, network order (0 = none)
    std::atomic<int> devicePortAtomic_{9000};
    std::atomic<bool> feedbackOn_{false}, onlyFromDevice_{false};
    std::unordered_map<std::string, float> lastSentValue_;   // feedback thread + echo suppression (oscMutex_)
    std::atomic<bool> feedbackRunning_{false};
    std::thread feedbackThread_;
    std::uintptr_t sendSocket_ = ~std::uintptr_t(0);
};

} // namespace SonusDSP
