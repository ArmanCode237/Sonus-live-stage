// Standalone app only: audio output and MIDI inputs through RtAudio / RtMidi
// (HardwareManager). The plug-in never compiles this file.
#include "app/AudioBackend.h"
#include "app/HardwareManager.h"

namespace {
class RtAudioBackend final : public AudioBackend {
public:
    explicit RtAudioBackend(SonusDSP::WorkstationHost& engine) : hw_(std::make_unique<HardwareManager>(engine)) {}
    const char* hostKind() const override { return "app"; }
    nlohmann::json statusJson() override { auto j = hw_->getHardwareStatusJson(); j["host"] = "app"; return j; }
    void poll() override { hw_->pollHotPlugIfNeeded(); hw_->checkAudioWatchdog(); }
    bool audioRecovering() const override { return hw_->audioRecovering(); }
    int audioRecoveries() const override { return hw_->audioRecoveries(); }
    void rescanAll() override { hw_->scanAllAudioDevices(); hw_->restoreOrInitAudio(); hw_->refreshMidiPorts(); }
    bool configureAudio(int uid, unsigned sampleRate, unsigned bufferSize, std::string& message) override {
        return hw_->configureAudio(uid, sampleRate, bufferSize, message);
    }
    void refreshMidiPorts() override { hw_->refreshMidiPorts(); }
    void forgetMidiPort(const std::string& cleanName) override { hw_->forgetDisconnectedMidiPort(cleanName); }
    bool setMidiPortEnabledByName(const std::string& cleanName, bool enabled) override { return hw_->setMidiPortEnabledByName(cleanName, enabled); }
    bool setMidiPortEnabledByIndex(int index, bool enabled) override { return hw_->setMidiPortEnabledByIndex(index, enabled); }
    std::vector<MidiSource> midiSources() override {
        std::vector<MidiSource> out;
        for (const auto& p : hw_->midiSourcesSnapshot()) out.push_back({p.name, p.id, p.connected, p.enabled});
        return out;
    }
private:
    std::unique_ptr<HardwareManager> hw_;
};
}

std::unique_ptr<AudioBackend> makeRtAudioBackend(SonusDSP::WorkstationHost& engine) {
    return std::make_unique<RtAudioBackend>(engine);
}
