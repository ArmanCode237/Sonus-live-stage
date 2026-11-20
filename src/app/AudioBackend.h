#pragma once
// v33 (V0): where audio and MIDI come from. The app core (StageApp) talks only to this
// interface, so the same core runs in the standalone app (RtAudio / RtMidi devices,
// RtAudioBackend.cpp) and in the VST3 / AU plug-in (the DAW owns audio and MIDI).
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace SonusDSP { class WorkstationHost; }

class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    /** "app" (own audio / MIDI devices) or "plugin" (the DAW provides them). */
    virtual const char* hostKind() const = 0;
    /** GET /api/hardware. Always has connectedEnabledMidiCount, disconnectedEnabledMidiCount,
     *  savedAudioMissing, audioDevices[] and midiPorts[] (the UI reads them). */
    virtual nlohmann::json statusJson() = 0;
    /** Every 500 ms off the audio thread: MIDI hot-plug and audio watchdog. */
    virtual void poll() {}
    virtual bool audioRecovering() const { return false; }
    virtual int audioRecoveries() const { return 0; }
    virtual void rescanAll() {}
    virtual bool configureAudio(int uid, unsigned sampleRate, unsigned bufferSize, std::string& message) {
        (void)uid; (void)sampleRate; (void)bufferSize;
        message = "El audio lo gestiona el DAW.";
        return false;
    }
    virtual void refreshMidiPorts() {}
    virtual void forgetMidiPort(const std::string& cleanName) { (void)cleanName; }
    virtual bool setMidiPortEnabledByName(const std::string& cleanName, bool enabled) { (void)cleanName; (void)enabled; return false; }
    virtual bool setMidiPortEnabledByIndex(int index, bool enabled) { (void)index; (void)enabled; return false; }
    /** v33 (M2): MIDI input ports opened this session and their routing source ids (1..15). */
    struct MidiSource { std::string name; int id = 0; bool connected = false; bool enabled = false; };
    virtual std::vector<MidiSource> midiSources() { return {}; }
};

using AudioBackendFactory = std::function<std::unique_ptr<AudioBackend>(SonusDSP::WorkstationHost&)>;

/** Plug-in hosts: no devices of its own; audio and MIDI arrive through the DAW. */
std::unique_ptr<AudioBackend> makeHostedAudioBackend(SonusDSP::WorkstationHost& engine);
/** Standalone app: RtAudio output + RtMidi inputs (defined in RtAudioBackend.cpp, app only). */
std::unique_ptr<AudioBackend> makeRtAudioBackend(SonusDSP::WorkstationHost& engine);
