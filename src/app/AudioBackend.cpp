#include "app/AudioBackend.h"
#include "core/WorkstationHost.h"

namespace {
// The plug-in: the DAW renders the audio and delivers MIDI (one source, the track).
class HostedAudioBackend final : public AudioBackend {
public:
    explicit HostedAudioBackend(SonusDSP::WorkstationHost& engine) : engine_(engine) {}
    const char* hostKind() const override { return "plugin"; }
    nlohmann::json statusJson() override {
        return {
            {"host", "plugin"},
            {"availableApis", nlohmann::json::array()}, {"asioAvailable", false},
            {"audioProbeErrors", nlohmann::json::array()}, {"lastAudioError", ""},
            {"activeApi", "DAW"}, {"activeAudioUid", -1}, {"activeAudioDeviceName", "DAW (plug-in)"},
            {"sampleRate", 0}, {"bufferSize", 0}, {"latencyMs", 0.0},
            {"audioUnderflows", engine_.getAudioUnderflows()},
            {"audioOverBudgetCallbacks", engine_.getAudioOverBudgetCallbacks()},
            {"maxAudioRenderMicros", engine_.getMaxAudioRenderMicros()},
            {"savedAudioMissing", false}, {"savedAudioDeviceName", ""}, {"savedAudioApi", ""},
            {"audioDevices", nlohmann::json::array()}, {"midiPorts", nlohmann::json::array()},
            {"connectedEnabledMidiCount", 1}, {"disconnectedEnabledMidiCount", 0},
        };
    }
private:
    SonusDSP::WorkstationHost& engine_;
};
}

std::unique_ptr<AudioBackend> makeHostedAudioBackend(SonusDSP::WorkstationHost& engine) {
    return std::make_unique<HostedAudioBackend>(engine);
}
