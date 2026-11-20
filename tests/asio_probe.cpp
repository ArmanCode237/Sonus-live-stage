// Live Stage standalone audio diagnostics (does NOT open an audio stream).
// Build on Windows: cmake --build build/win64 --config Release --target LiveStageAudioProbe
#include <RtAudio.h>
#include <algorithm>
#include <exception>
#include <iostream>
#include <vector>

int main() {
    std::vector<RtAudio::Api> compiled;
    RtAudio::getCompiledApi(compiled);
    const bool asio = std::find(compiled.begin(), compiled.end(), RtAudio::WINDOWS_ASIO) != compiled.end();
    std::cout << "RtAudio " << RtAudio::getVersion() << "\n"
              << "ASIO compiled: " << (asio ? "YES" : "NO") << "\n";
    unsigned int asioDevices = 0;
    for (const auto api : compiled) {
        if (api != RtAudio::WINDOWS_ASIO && api != RtAudio::WINDOWS_WASAPI &&
            api != RtAudio::MACOSX_CORE) continue;
        const std::string apiName = RtAudio::getApiDisplayName(api);
        std::cout << "\n[" << apiName << "]\n";
        try {
            RtAudio audio(api);
            for (const auto id : audio.getDeviceIds()) {
                const auto d = audio.getDeviceInfo(id);
                if (d.outputChannels == 0) continue;
                if (api == RtAudio::WINDOWS_ASIO) ++asioDevices;
                std::cout << "  id=" << id << " name=" << d.name
                          << " outputs=" << d.outputChannels
                          << " preferred=" << d.preferredSampleRate << "Hz rates=";
                for (unsigned int rate : d.sampleRates) std::cout << rate << " ";
                std::cout << "\n";
            }
        } catch (const std::exception &e) {
            std::cerr << "  error: " << e.what() << "\n";
        }
    }
    std::cout << "\nASIO output drivers: " << asioDevices << "\n";
    if (!asio) std::cerr << "ASIO not in binary: configure -DSTAGE_ENABLE_ASIO=ON and rebuild.\n";
    else if (asioDevices == 0) std::cerr << "No ASIO driver found: install/repair native interface driver and reconnect USB.\n";
    return !asio ? 2 : (asioDevices == 0 ? 3 : 0);
}
