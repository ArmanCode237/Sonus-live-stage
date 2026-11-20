#include "MidiOscMapper.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <chrono>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
static void closeSocket(SocketHandle s) { closesocket(s); }
static const SocketHandle kNoSocket = INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
using SocketHandle = int;
static void closeSocket(SocketHandle s) { close(s); }
static const SocketHandle kNoSocket = -1;
#endif

namespace SonusDSP {

namespace {
uint32_t readBE32(const char* p) {
    return (uint32_t(uint8_t(p[0])) << 24) | (uint32_t(uint8_t(p[1])) << 16) | (uint32_t(uint8_t(p[2])) << 8) | uint8_t(p[3]);
}
// OSC strings are NUL-terminated and padded to 4 bytes.
bool readOscString(const char* data, size_t size, size_t& pos, std::string& value) {
    if (pos >= size) return false;
    const size_t begin = pos;
    while (pos < size && data[pos] != '\0') ++pos;
    if (pos == size || pos - begin > 255) return false;
    value.assign(data + begin, pos - begin);
    pos = (pos + 4) & ~size_t(3);
    return pos <= size;
}
void appendOscString(std::string& out, const std::string& s) {
    out += s; out.push_back('\0');
    while (out.size() % 4) out.push_back('\0');
}
std::string ipToString(uint32_t networkOrder) {
    in_addr a{}; a.s_addr = networkOrder;
    char buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a, buf, sizeof buf);
    return buf;
}
} // namespace

MidiOscMapper::MidiOscMapper(WorkstationHost& host) : host_(host) {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    const SocketHandle s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sendSocket_ = std::uintptr_t(s);
    feedbackRunning_ = true;
    feedbackThread_ = std::thread(&MidiOscMapper::feedbackLoop, this);
}

MidiOscMapper::~MidiOscMapper() {
    stopOscServer();
    feedbackRunning_ = false;
    if (feedbackThread_.joinable()) feedbackThread_.join();
    if (SocketHandle(sendSocket_) != kNoSocket) closeSocket(SocketHandle(sendSocket_));
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool MidiOscMapper::startOscServer(int udpPort) {
    stopOscServer();
    oscPort_.store(udpPort);
    oscRunning_.store(true);
    oscThread_ = std::thread(&MidiOscMapper::oscThreadLoop, this, udpPort);
    return true;
}

void MidiOscMapper::stopOscServer() {
    oscRunning_.store(false);
    if (oscThread_.joinable()) oscThread_.join();
}

std::string MidiOscMapper::getLastOscAddress() const {
    std::lock_guard<std::mutex> lock(oscMutex_);
    return lastOscAddress_;
}

void MidiOscMapper::oscThreadLoop(int port) {
    const SocketHandle sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == kNoSocket) { oscRunning_.store(false); return; }
#if defined(_WIN32)
    DWORD timeout = 250;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv{0, 250000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = INADDR_ANY;            // every network interface (LAN controllers)
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        closeSocket(sock);
        std::cerr << "[OSC] No se pudo abrir el puerto UDP " << port << " (¿ocupado?)\n";
        oscRunning_.store(false);
        return;
    }
    std::cout << "[OSC] Servidor UDP escuchando en el puerto " << port << " (todas las interfaces)\n";
    char buffer[8192];
    while (oscRunning_.load()) {
        sockaddr_in sender{};
#if defined(_WIN32)
        int senderLen = sizeof(sender);
#else
        socklen_t senderLen = sizeof(sender);
#endif
        const int received = recvfrom(sock, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&sender), &senderLen);
        if (received <= 4) continue;
        const uint32_t from = sender.sin_addr.s_addr;
        if (onlyFromDevice_.load() && from != deviceAddr_.load()) {
            std::lock_guard<std::mutex> lock(oscMutex_);
            ++rejected_;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(oscMutex_);
            lastSender_ = ipToString(from);
            ++received_;
        }
        handlePacket(buffer, size_t(received), 0);
    }
    closeSocket(sock);
}

// "#bundle\0" + 8-byte time tag + [int32 size + element]..., elements may be bundles again.
void MidiOscMapper::handlePacket(const char* data, size_t len, int depth) {
    if (!data || len < 4 || depth > 8) return;
    if (data[0] == '/') { handleMessage(data, len); return; }
    if (len >= 16 && std::memcmp(data, "#bundle\0", 8) == 0) {
        size_t pos = 16;
        while (pos + 4 <= len) {
            const uint32_t size = readBE32(data + pos);
            pos += 4;
            if (size == 0 || size > len - pos || (size & 3)) return;
            handlePacket(data + pos, size, depth + 1);
            pos += size;
        }
    }
}

void MidiOscMapper::handleMessage(const char* data, size_t size) {
    size_t at = 0;
    std::string address, tags;
    if (!readOscString(data, size, at, address)) return;
    float value = 1.f;                            // no argument: a trigger (button press)
    if (at < size && readOscString(data, size, at, tags) && tags.size() >= 2 && tags[0] == ',') {
        switch (tags[1]) {
            case 'f': if (at + 4 > size) return; { const uint32_t raw = readBE32(data + at); std::memcpy(&value, &raw, 4); } break;
            case 'i': if (at + 4 > size) return; { const int32_t v = int32_t(readBE32(data + at)); value = float(v); } break;
            case 'd': if (at + 8 > size) return; {
                const uint64_t raw = (uint64_t(readBE32(data + at)) << 32) | readBE32(data + at + 4);
                double d; std::memcpy(&d, &raw, 8); value = float(d); } break;
            case 'h': if (at + 8 > size) return; {
                const int64_t v = int64_t((uint64_t(readBE32(data + at)) << 32) | readBE32(data + at + 4)); value = float(v); } break;
            case 'T': value = 1.f; break;
            case 'F': value = 0.f; break;
            default: return;                      // strings, blobs: not a control value
        }
    }
    if (!std::isfinite(value)) return;
    {
        std::lock_guard<std::mutex> lock(oscMutex_);
        lastOscAddress_ = address;
        lastSentValue_[address] = value;          // no echo of what the controller just sent
    }
    // Mapped addresses (any scope) receive normalised 0..1 values.
    if (host_.getMappings().handleOsc(address, value)) return;
    // Direct addressing is always raw (unit values).
    const std::string paramId = (address.rfind("/param/", 0) == 0) ? address.substr(7) : address.substr(1);
    host_.applyParamById(paramId, value, false);
}

void MidiOscMapper::setDevice(const std::string& ip, int port, bool feedback, bool onlyFromDevice) {
    in_addr parsed{};
    const bool valid = !ip.empty() && inet_pton(AF_INET, ip.c_str(), &parsed) == 1;
    std::lock_guard<std::mutex> lock(oscMutex_);
    device_.deviceIp = valid ? ip : std::string();
    device_.devicePort = std::clamp(port, 1, 65535);
    device_.feedback = feedback;
    device_.onlyFromDevice = onlyFromDevice && valid;
    deviceAddr_ = valid ? parsed.s_addr : 0;
    devicePortAtomic_ = device_.devicePort;
    feedbackOn_ = feedback && valid;
    onlyFromDevice_ = device_.onlyFromDevice;
    lastSentValue_.clear();                       // a new device gets every value once
}

OscDeviceConfig MidiOscMapper::deviceConfig() const {
    std::lock_guard<std::mutex> lock(oscMutex_);
    return device_;
}

bool MidiOscMapper::sendToDevice(const std::string& address, float value) {
    const uint32_t ip = deviceAddr_.load();
    if (!ip || SocketHandle(sendSocket_) == kNoSocket || address.empty() || address[0] != '/') return false;
    std::string packet;
    appendOscString(packet, address);
    appendOscString(packet, ",f");
    uint32_t raw; std::memcpy(&raw, &value, 4);
    for (int shift = 24; shift >= 0; shift -= 8) packet.push_back(char((raw >> shift) & 0xFF));
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(uint16_t(devicePortAtomic_.load()));
    to.sin_addr.s_addr = ip;
    const int n = sendto(SocketHandle(sendSocket_), packet.data(), int(packet.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
    if (n > 0) { std::lock_guard<std::mutex> lock(oscMutex_); ++sent_; }
    return n > 0;
}

// Every 40 ms: the normalised value of each OSC-mapped parameter that changed goes back
// to the device, so motorised faders / TouchOSC widgets follow the app.
void MidiOscMapper::feedbackLoop() {
    while (feedbackRunning_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        if (!feedbackOn_.load()) continue;
        const auto values = host_.getMappings().oscFeedback();
        for (const auto& v : values) {
            bool changed;
            {
                std::lock_guard<std::mutex> lock(oscMutex_);
                auto it = lastSentValue_.find(v.address);
                changed = it == lastSentValue_.end() || std::abs(it->second - v.value01) > 1e-3f;
                if (changed) lastSentValue_[v.address] = v.value01;
            }
            if (changed) sendToDevice(v.address, v.value01);
        }
    }
}

std::vector<std::string> MidiOscMapper::localAddresses() {
    std::vector<std::string> out;
    char host[256] = {};
    if (gethostname(host, sizeof host) != 0) return out;
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    addrinfo* list = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &list) != 0) return out;
    for (addrinfo* a = list; a; a = a->ai_next) {
        const auto* sin = reinterpret_cast<const sockaddr_in*>(a->ai_addr);
        const std::string ip = ipToString(sin->sin_addr.s_addr);
        if (ip.rfind("127.", 0) != 0 && ip.rfind("169.254.", 0) != 0 &&
            std::find(out.begin(), out.end(), ip) == out.end()) out.push_back(ip);
    }
    freeaddrinfo(list);
    return out;
}

nlohmann::json MidiOscMapper::status() const {
    std::lock_guard<std::mutex> lock(oscMutex_);
    return {{"running", oscRunning_.load()}, {"port", oscPort_.load()}, {"localAddresses", localAddresses()},
            {"deviceIp", device_.deviceIp}, {"devicePort", device_.devicePort}, {"feedback", device_.feedback},
            {"onlyFromDevice", device_.onlyFromDevice}, {"lastAddress", lastOscAddress_}, {"lastSender", lastSender_},
            {"received", received_}, {"rejected", rejected_}, {"sent", sent_}};
}

} // namespace SonusDSP
