#include "MidiOscMapper.h"
#include <iostream>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace NordDSP {

MidiOscMapper::MidiOscMapper(WorkstationHost& host) : host_(host) {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

MidiOscMapper::~MidiOscMapper() {
    stopOscServer();
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
    if (oscThread_.joinable()) {
        oscThread_.join();
    }
}

std::string MidiOscMapper::getLastOscAddress() const {
    std::lock_guard<std::mutex> lock(oscMutex_);
    return lastOscAddress_;
}

void MidiOscMapper::oscThreadLoop(int port) {
#if defined(_WIN32)
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) { oscRunning_.store(false); return; }
    DWORD timeout = 250;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { oscRunning_.store(false); return; }
    struct timeval tv{0, 250000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
#if defined(_WIN32)
        closesocket(sock);
#else
        close(sock);
#endif
        oscRunning_.store(false);
        return;
    }

    std::cout << "[OSC] Servidor UDP escuchando en el puerto " << port << "\n";
    char buffer[1024];
    while (oscRunning_.load()) {
        sockaddr_in sender{};
#if defined(_WIN32)
        int senderLen = sizeof(sender);
#else
        socklen_t senderLen = sizeof(sender);
#endif
        int received = recvfrom(sock, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&sender), &senderLen);
        if (received > 4) {
            parseOscPacket(buffer, received);
        }
    }

#if defined(_WIN32)
    closesocket(sock);
#else
    close(sock);
#endif
}

void MidiOscMapper::parseOscPacket(const char* buf, int len) {
    if (buf[0] != '/') return;
    std::string addr(buf);
    {
        std::lock_guard<std::mutex> lock(oscMutex_);
        lastOscAddress_ = addr;
    }

    size_t pos = ((addr.size() + 4) / 4) * 4;
    if (pos >= static_cast<size_t>(len) || buf[pos] != ',') return;

    char typeTag = buf[pos + 1];
    size_t argPos = pos + 4;
    if (argPos + 4 > static_cast<size_t>(len)) return;

    float val = 0.0f;
    if (typeTag == 'f') {
        uint32_t be = (static_cast<uint8_t>(buf[argPos]) << 24) |
                      (static_cast<uint8_t>(buf[argPos + 1]) << 16) |
                      (static_cast<uint8_t>(buf[argPos + 2]) << 8) |
                       static_cast<uint8_t>(buf[argPos + 3]);
        std::memcpy(&val, &be, sizeof(float));
    } else if (typeTag == 'i') {
        int32_t be = (static_cast<uint8_t>(buf[argPos]) << 24) |
                     (static_cast<uint8_t>(buf[argPos + 1]) << 16) |
                     (static_cast<uint8_t>(buf[argPos + 2]) << 8) |
                      static_cast<uint8_t>(buf[argPos + 3]);
        val = static_cast<float>(be);
    }

    // Soporta direcciones OSC tipo "/param/cutoff" o directamente "/cutoff"
    std::string paramId = (addr.rfind("/param/", 0) == 0) ? addr.substr(7) : addr.substr(1);
    host_.applyParamById(paramId, val, false);
}

} // namespace NordDSP