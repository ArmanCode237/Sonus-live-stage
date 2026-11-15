#include "MidiOscMapper.h"
#include <iostream>
#include <cstring>
#include <cmath>

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

void MidiOscMapper::parseOscPacket(const char* data,int len){
    // OSC padded strings are not necessarily terminated within UDP payload.
    if(!data||len<8||data[0]!='/')return;
    const auto size=static_cast<size_t>(len);
    auto readString=[&](size_t& pos,std::string& value)->bool{
        if(pos>=size)return false;
        const size_t begin=pos;
        while(pos<size&&data[pos]!='\0')++pos;
        if(pos==size||pos-begin>255)return false;
        value.assign(data+begin,pos-begin);
        pos=(pos+4)&~size_t(3);
        return pos<=size;
    };
    size_t at=0;std::string address,tags;
    if(!readString(at,address)||!readString(at,tags)||tags.size()<2||tags[0]!=',')return;
    if(at+4>size)return;
    const auto readBE=[&](){return (uint32_t(uint8_t(data[at]))<<24)|
        (uint32_t(uint8_t(data[at+1]))<<16)|(uint32_t(uint8_t(data[at+2]))<<8)|uint8_t(data[at+3]);};
    const uint32_t raw=readBE();
    float value=0;
    if(tags[1]=='f'){std::memcpy(&value,&raw,sizeof(value));}
    else if(tags[1]=='i'){int32_t signedValue;std::memcpy(&signedValue,&raw,sizeof(raw));value=float(signedValue);}
    else return;
    if(!std::isfinite(value))return;
    {
        std::lock_guard<std::mutex> lock(oscMutex_);
        lastOscAddress_=address;
    }
    // Mapped addresses (any scope) receive normalised 0..1 values.
    if(host_.getMappings().handleOsc(address,value))return;
    // Direct addressing is always raw (unit values).
    const std::string paramId=(address.rfind("/param/",0)==0)?address.substr(7):address.substr(1);
    host_.applyParamById(paramId,value,false);
}
} // namespace NordDSP
