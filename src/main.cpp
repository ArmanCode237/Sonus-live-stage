#include "loaders/BankLoader.h"
#include "core/WorkstationHost.h"
#include "control/MidiOscMapper.h"
#include "modules/NordPianoEngine.h"
#include "httplib.h"
#include <nlohmann/json.hpp>
#include <RtAudio.h>
#include <RtMidi.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <mutex>
#include <set>
#include <chrono>
#include <cstdlib>
#include <cctype>

using namespace NordDSP;
using json = nlohmann::json;
namespace fs = std::filesystem;

int audioCallback(void* outputBuffer, void* /*inputBuffer*/, unsigned int nBufferFrames,
                  double /*streamTime*/, RtAudioStreamStatus /*status*/, void* userData) {
    auto* engine = static_cast<WorkstationHost*>(userData);
    auto* out = static_cast<float*>(outputBuffer);
    engine->render(out, nBufferFrames);
    return 0;
}

void midiCallback(double /*deltatime*/, std::vector<unsigned char>* message, void* userData) {
    if (!message || message->size() < 2) return;
    auto* host = static_cast<WorkstationHost*>(userData);

    unsigned char status = (*message)[0] & 0xF0;
    unsigned char data1  = (*message)[1] & 0x7F;
    unsigned char data2  = (message->size() > 2) ? ((*message)[2] & 0x7F) : 0;

    if (status == 0x90 && data2 > 0) {
        host->noteOn(data1, data2);
    } else if (status == 0x80 || (status == 0x90 && data2 == 0)) {
        host->noteOff(data1);
    } else if (status == 0xE0 && message->size() >= 3) {
        const int bend14 = (static_cast<int>(data2) << 7) | static_cast<int>(data1);
        const float centered = bend14 >= 8192 ? (bend14 - 8192) / 8191.f : (bend14 - 8192) / 8192.f;
        host->setPitchBend(centered); // ±2 semitones, center=8192
    } else if (status == 0xB0) {
        // CC 1 and sustain are performance controls and should not be swallowed by MIDI Learn.
        if (data1 == 1) { host->setModWheel(data2 / 127.f); return; }
        if (data1 == 121) { host->setModWheel(0.f); host->setPitchBend(0.f); return; }
        if (host->getRegistry().handleMidiCC(data1, data2)) return;
        if (data1 == 64) host->setSustainPedal(data2 >= 64);
        else if (data1 == 7) host->setMasterGain(static_cast<float>(data2) / 100.0f);
        else if (data1 == 10) host->setPan((static_cast<float>(data2) - 64.0f) / 63.0f);
        else if (data1 == 74) {
            float norm = static_cast<float>(data2) / 127.0f;
            host->setCutoffHz(20.0f * std::pow(1000.0f, norm));
        } else if (data1 == 71) {
            host->setResonanceQ(0.5f + (static_cast<float>(data2) / 127.0f) * 11.5f);
        } else if (data1 == 120 || data1 == 123) {
            host->allNotesOff();
        }
    }
}

std::string normalizeMidiPortName(const std::string& rawName) {
    std::string s = rawName;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    auto lastSpace = s.find_last_of(' ');
    if (lastSpace != std::string::npos && lastSpace + 1 < s.size()) {
        bool allDigits = true;
        for (size_t i = lastSpace + 1; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) { allDigits = false; break; }
        }
        if (allDigits) {
            s = s.substr(0, lastSpace);
        }
    }
    return s;
}

struct MidiPortSlot {
    int portIndex = -1;
    std::string rawName;
    std::string cleanName;
    bool connected = false;
    bool enabled = false;
    std::unique_ptr<RtMidiIn> handle;
};

struct AudioDeviceSpec {
    int uid = 0;
    RtAudio::Api api;
    std::string apiName;
    unsigned int rtDeviceId = 0;
    std::string name;
    unsigned int preferredSr = 48000;
    std::vector<unsigned int> supportedSampleRates;
    unsigned int outputChannels = 2;
    bool isDefault = false;
};

class HardwareManager {
public:
    explicit HardwareManager(WorkstationHost& eng) : engine_(eng) {
        loadSavedConfig();
        scanAllAudioDevices();
        restoreOrInitAudio();
        refreshMidiPorts();
        lastHotPlugCheck_ = std::chrono::steady_clock::now();
    }

    ~HardwareManager() {
        stopAudio();
    }

    void pollHotPlugIfNeeded() {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHotPlugCheck_).count() < 1500) {
            return;
        }
        lastHotPlugCheck_ = now;
        refreshMidiPorts();
    }

    void scanAllAudioDevices() {
        std::lock_guard<std::mutex> lock(hwMutex_);
        stopAudioInternal();
        devices_.clear();
        availableApis_.clear();

        std::vector<RtAudio::Api> apis;
        RtAudio::getCompiledApi(apis);
        int nextUid = 0;

        auto probeApi = [&](RtAudio::Api api, const std::string& apiLabel) {
            if (std::find(apis.begin(), apis.end(), api) == apis.end()) return;
            availableApis_.push_back(apiLabel);
            try {
                RtAudio tempDac(api);
                tempDac.showWarnings(false);
                unsigned int defId = tempDac.getDefaultOutputDevice();
                for (auto id : tempDac.getDeviceIds()) {
                    auto info = tempDac.getDeviceInfo(id);
                    if (info.outputChannels >= 2) {
                        std::vector<unsigned int> srs = info.sampleRates;
                        if (srs.empty()) srs = {44100, 48000, 88200, 96000};
                        unsigned int pref = info.preferredSampleRate ? info.preferredSampleRate : 48000;
                        devices_.push_back({
                            nextUid++,
                            api,
                            apiLabel,
                            id,
                            info.name,
                            pref,
                            srs,
                            info.outputChannels,
                            (id == defId && apiLabel == "WASAPI")
                        });
                    }
                }
            } catch (...) {}
        };

#if defined(_WIN32)
        probeApi(RtAudio::WINDOWS_WASAPI, "WASAPI");
        probeApi(RtAudio::WINDOWS_ASIO, "ASIO");
#elif defined(__APPLE__)
        probeApi(RtAudio::MACOSX_CORE, "CoreAudio");
#endif
    }

    void restoreOrInitAudio() {
        std::lock_guard<std::mutex> lock(hwMutex_);
        std::string msg;

        if (!savedAudioDeviceName_.empty()) {
            int foundUid = -1;
            for (const auto& d : devices_) {
                if (d.apiName == savedAudioApi_ && d.name == savedAudioDeviceName_) {
                    foundUid = d.uid;
                    break;
                }
            }
            if (foundUid >= 0 && tryOpenInternal(foundUid, savedSampleRate_, savedBufferSize_, msg)) {
                savedAudioMissing_ = false;
                return;
            }
            savedAudioMissing_ = true;
            std::cerr << "[Audio] Dispositivo guardado '" << savedAudioDeviceName_
                      << "' no disponible. Usando salida de respaldo...\n";
        }

        tryOpenInternal(-1, 0, 128, msg);
    }

    bool configureAudio(int uid, unsigned int requestedSr, unsigned int requestedBuffer, std::string& outMsg) {
        std::lock_guard<std::mutex> lock(hwMutex_);
        if (devices_.empty()) {
            outMsg = "No hay dispositivos de audio disponibles.";
            return false;
        }

        int prevUid = activeUid_;
        unsigned int prevSr = activeSampleRate_;
        unsigned int prevBuf = activeBufferSize_;

        if (tryOpenInternal(uid, requestedSr, requestedBuffer, outMsg)) {
            savedAudioApi_ = activeApiName_;
            savedAudioDeviceName_ = activeDeviceName_;
            savedSampleRate_ = activeSampleRate_;
            savedBufferSize_ = activeBufferSize_;
            savedAudioMissing_ = false;
            saveConfigInternal();
            return true;
        }

        std::string failReason = outMsg;
        if (prevUid >= 0 && prevUid != uid) {
            std::string dummy;
            tryOpenInternal(prevUid, prevSr, prevBuf, dummy);
        } else {
            std::string dummy;
            tryOpenInternal(-1, 0, 128, dummy);
        }
        outMsg = failReason + " (Se restauró la salida activa anterior).";
        return false;
    }

    void stopAudio() {
        std::lock_guard<std::mutex> lock(hwMutex_);
        stopAudioInternal();
    }

    void refreshMidiPorts() {
        std::lock_guard<std::mutex> lock(hwMutex_);

        std::vector<std::pair<unsigned int, std::string>> currentHwPorts;
        try {
            RtMidiIn probe;
            unsigned int count = probe.getPortCount();
            for (unsigned int i = 0; i < count; ++i) {
                currentHwPorts.push_back({i, probe.getPortName(i)});
            }
        } catch (...) {}

        for (auto& slot : midiSlots_) {
            bool stillThere = false;
            for (const auto& [hwIdx, hwRawName] : currentHwPorts) {
                if (normalizeMidiPortName(hwRawName) == slot.cleanName) {
                    stillThere = true;
                    slot.portIndex = static_cast<int>(hwIdx);
                    slot.rawName = hwRawName;
                    break;
                }
            }
            if (!stillThere) {
                if (slot.connected) {
                    std::cout << "[MIDI] Alerta: Teclado desconectado -> " << slot.cleanName << "\n";
                }
                slot.handle.reset();
                slot.connected = false;
                slot.portIndex = -1;
            }
        }

        for (const auto& [hwIdx, hwRawName] : currentHwPorts) {
            std::string clean = normalizeMidiPortName(hwRawName);
            auto it = std::find_if(midiSlots_.begin(), midiSlots_.end(),
                                   [&](const MidiPortSlot& s) { return s.cleanName == clean; });

            bool shouldBeEnabled = (savedEnabledMidiPorts_.count(clean) > 0);

            if (it == midiSlots_.end()) {
                MidiPortSlot slot;
                slot.portIndex = static_cast<int>(hwIdx);
                slot.rawName = hwRawName;
                slot.cleanName = clean;
                slot.connected = true;
                slot.enabled = shouldBeEnabled;

                if (shouldBeEnabled) {
                    try {
                        slot.handle = std::make_unique<RtMidiIn>();
                        slot.handle->openPort(hwIdx);
                        slot.handle->ignoreTypes(true, true, true);
                        slot.handle->setCallback(&midiCallback, &engine_);
                        std::cout << "[MIDI] Reconectado automáticamente: " << clean << "\n";
                    } catch (...) {
                        slot.handle.reset();
                        slot.connected = false;
                    }
                }
                midiSlots_.push_back(std::move(slot));
            } else {
                it->portIndex = static_cast<int>(hwIdx);
                it->rawName = hwRawName;
                if (!it->connected) {
                    it->connected = true;
                    std::cout << "[MIDI] Teclado detectado nuevamente: " << clean << "\n";
                }
                if (it->enabled && !it->handle) {
                    try {
                        it->handle = std::make_unique<RtMidiIn>();
                        it->handle->openPort(hwIdx);
                        it->handle->ignoreTypes(true, true, true);
                        it->handle->setCallback(&midiCallback, &engine_);
                    } catch (...) {
                        it->handle.reset();
                    }
                }
            }
        }

        for (const auto& savedClean : savedKnownMidiPorts_) {
            auto it = std::find_if(midiSlots_.begin(), midiSlots_.end(),
                                   [&](const MidiPortSlot& s) { return s.cleanName == savedClean; });
            if (it == midiSlots_.end()) {
                MidiPortSlot disconnectedSlot;
                disconnectedSlot.portIndex = -1;
                disconnectedSlot.rawName = savedClean;
                disconnectedSlot.cleanName = savedClean;
                disconnectedSlot.connected = false;
                disconnectedSlot.enabled = (savedEnabledMidiPorts_.count(savedClean) > 0);
                midiSlots_.push_back(std::move(disconnectedSlot));
            }
        }
    }

    bool setMidiPortEnabledByName(const std::string& cleanName, bool enable) {
        std::lock_guard<std::mutex> lock(hwMutex_);
        savedKnownMidiPorts_.insert(cleanName);
        if (enable) savedEnabledMidiPorts_.insert(cleanName);
        else savedEnabledMidiPorts_.erase(cleanName);

        for (auto& slot : midiSlots_) {
            if (slot.cleanName == cleanName) {
                slot.enabled = enable;
                if (enable && slot.connected && slot.portIndex >= 0 && !slot.handle) {
                    try {
                        slot.handle = std::make_unique<RtMidiIn>();
                        slot.handle->openPort(static_cast<unsigned int>(slot.portIndex));
                        slot.handle->ignoreTypes(true, true, true);
                        slot.handle->setCallback(&midiCallback, &engine_);
                        std::cout << "[MIDI] Activado: " << slot.cleanName << "\n";
                    } catch (...) {
                        slot.handle.reset();
                        return false;
                    }
                } else if (!enable && slot.handle) {
                    slot.handle.reset();
                    std::cout << "[MIDI] Desactivado: " << slot.cleanName << "\n";
                }
                saveConfigInternal();
                return true;
            }
        }
        saveConfigInternal();
        return false;
    }

    bool setMidiPortEnabledByIndex(int portIdx, bool enable) {
        std::string foundName;
        {
            std::lock_guard<std::mutex> lock(hwMutex_);
            for (const auto& slot : midiSlots_) {
                if (slot.portIndex == portIdx) {
                    foundName = slot.cleanName;
                    break;
                }
            }
        }
        if (!foundName.empty()) {
            return setMidiPortEnabledByName(foundName, enable);
        }
        return false;
    }

    void forgetDisconnectedMidiPort(const std::string& cleanName) {
        std::lock_guard<std::mutex> lock(hwMutex_);
        savedKnownMidiPorts_.erase(cleanName);
        savedEnabledMidiPorts_.erase(cleanName);
        midiSlots_.erase(
            std::remove_if(midiSlots_.begin(), midiSlots_.end(),
                           [&](const MidiPortSlot& s) { return !s.connected && s.cleanName == cleanName; }),
            midiSlots_.end()
        );
        saveConfigInternal();
    }

    json getHardwareStatusJson() {
        pollHotPlugIfNeeded();
        std::lock_guard<std::mutex> lock(hwMutex_);
        json j;
        j["availableApis"] = availableApis_;
        j["activeApi"] = activeApiName_;
        j["activeAudioUid"] = activeUid_;
        j["activeAudioDeviceName"] = activeDeviceName_;
        j["sampleRate"] = activeSampleRate_;
        j["bufferSize"] = activeBufferSize_;
        j["latencyMs"] = (activeSampleRate_ > 0) ? ((static_cast<double>(activeBufferSize_) * 1000.0) / activeSampleRate_) : 0.0;
        j["savedAudioMissing"] = savedAudioMissing_;
        j["savedAudioDeviceName"] = savedAudioDeviceName_;
        j["savedAudioApi"] = savedAudioApi_;

        json audioDevs = json::array();
        for (const auto& d : devices_) {
            audioDevs.push_back({
                {"uid", d.uid},
                {"api", d.apiName},
                {"rtId", d.rtDeviceId},
                {"name", d.name},
                {"preferredSr", d.preferredSr},
                {"sampleRates", d.supportedSampleRates},
                {"channels", d.outputChannels},
                {"connected", true}
            });
        }
        j["audioDevices"] = audioDevs;

        json midiPorts = json::array();
        int disconnectedEnabledCount = 0;
        int connectedEnabledCount = 0;
        for (size_t i = 0; i < midiSlots_.size(); ++i) {
            const auto& s = midiSlots_[i];
            if (s.enabled && !s.connected) disconnectedEnabledCount++;
            if (s.enabled && s.connected) connectedEnabledCount++;
            midiPorts.push_back({
                {"index", s.portIndex},
                {"name", s.cleanName},
                {"rawName", s.rawName},
                {"connected", s.connected},
                {"enabled", s.enabled}
            });
        }
        j["midiPorts"] = midiPorts;
        j["connectedEnabledMidiCount"] = connectedEnabledCount;
        j["disconnectedEnabledMidiCount"] = disconnectedEnabledCount;
        return j;
    }

private:
    void loadSavedConfig() {
        if (!fs::exists(configFilePath_)) return;
        try {
            std::ifstream f(configFilePath_);
            json j = json::parse(f);
            if (j.contains("audio") && j["audio"].is_object()) {
                savedAudioApi_        = j["audio"].value("api", "");
                savedAudioDeviceName_ = j["audio"].value("deviceName", "");
                savedSampleRate_      = j["audio"].value("sampleRate", 48000u);
                savedBufferSize_      = j["audio"].value("bufferSize", 128u);
            }
            if (j.contains("midi") && j["midi"].is_object()) {
                if (j["midi"].contains("enabledPorts") && j["midi"]["enabledPorts"].is_array()) {
                    for (const auto& item : j["midi"]["enabledPorts"]) {
                        if (item.is_string()) {
                            std::string clean = normalizeMidiPortName(item.get<std::string>());
                            savedEnabledMidiPorts_.insert(clean);
                            savedKnownMidiPorts_.insert(clean);
                        }
                    }
                }
                if (j["midi"].contains("knownPorts") && j["midi"]["knownPorts"].is_array()) {
                    for (const auto& item : j["midi"]["knownPorts"]) {
                        if (item.is_string()) {
                            savedKnownMidiPorts_.insert(normalizeMidiPortName(item.get<std::string>()));
                        }
                    }
                }
            }
        } catch (...) {}
    }

    void saveConfigInternal() {
        try {
            json enabledArr = json::array();
            for (const auto& name : savedEnabledMidiPorts_) enabledArr.push_back(name);
            json knownArr = json::array();
            for (const auto& name : savedKnownMidiPorts_) knownArr.push_back(name);

            json j = {
                {"audio", {
                    {"api", savedAudioApi_.empty() ? activeApiName_ : savedAudioApi_},
                    {"deviceName", savedAudioDeviceName_.empty() ? activeDeviceName_ : savedAudioDeviceName_},
                    {"sampleRate", savedSampleRate_ ? savedSampleRate_ : activeSampleRate_},
                    {"bufferSize", savedBufferSize_ ? savedBufferSize_ : activeBufferSize_}
                }},
                {"midi", {
                    {"enabledPorts", enabledArr},
                    {"knownPorts", knownArr}
                }}
            };
            std::ofstream out(configFilePath_);
            if (out) out << j.dump(2);
        } catch (...) {}
    }

    bool tryOpenInternal(int uid, unsigned int requestedSr, unsigned int requestedBuffer, std::string& outMsg) {
        const AudioDeviceSpec* target = nullptr;
        for (const auto& d : devices_) {
            if (d.uid == uid) { target = &d; break; }
        }
        if (!target) {
            for (const auto& d : devices_) {
                if (d.isDefault) { target = &d; break; }
            }
        }
        if (!target && !devices_.empty()) target = &devices_[0];
        if (!target) return false;

        stopAudioInternal();

        try {
            dac_ = std::make_unique<RtAudio>(target->api);
            dac_->showWarnings(false);

            unsigned int sr = (requestedSr > 0) ? requestedSr : target->preferredSr;
            if (sr == 0) sr = 48000;
            unsigned int bufFrames = (requestedBuffer >= 32) ? requestedBuffer : 128;

            RtAudio::StreamParameters outParams;
            outParams.deviceId = target->rtDeviceId;
            outParams.nChannels = 2;
            outParams.firstChannel = 0;

            RtAudio::StreamOptions options;
            options.flags = RTAUDIO_SCHEDULE_REALTIME;

            if (dac_->openStream(&outParams, nullptr, RTAUDIO_FLOAT32, sr, &bufFrames, &audioCallback, &engine_, &options) != RTAUDIO_NO_ERROR) {
                outMsg = "El driver [" + target->apiName + "] " + target->name + " no está disponible.";
                return false;
            }
            if (dac_->startStream() != RTAUDIO_NO_ERROR) {
                outMsg = "No se pudo iniciar el stream en " + target->name + ".";
                return false;
            }

            unsigned int actualSr = dac_->getStreamSampleRate();
            if (actualSr == 0) actualSr = sr;
            engine_.setSampleRate(actualSr);

            activeUid_ = target->uid;
            activeApiName_ = target->apiName;
            activeDeviceName_ = target->name;
            activeSampleRate_ = actualSr;
            activeBufferSize_ = bufFrames;

            outMsg = "Activo: [" + activeApiName_ + "] " + activeDeviceName_ + " @ " +
                     std::to_string(actualSr) + " Hz (" + std::to_string(bufFrames) + " samples)";
            std::cout << "[Audio] " << outMsg << "\n";
            return true;
        } catch (...) {
            outMsg = "Excepción al inicializar el driver de audio.";
            return false;
        }
    }

    void stopAudioInternal() {
        if (dac_) {
            try {
                if (dac_->isStreamRunning()) dac_->stopStream();
                if (dac_->isStreamOpen()) dac_->closeStream();
            } catch (...) {}
            dac_.reset();
        }
    }

    WorkstationHost& engine_;
    std::unique_ptr<RtAudio> dac_;
    std::vector<std::string> availableApis_;
    std::vector<AudioDeviceSpec> devices_;
    int activeUid_ = -1;
    std::string activeApiName_ = "WASAPI";
    std::string activeDeviceName_ = "None";
    unsigned int activeSampleRate_ = 48000;
    unsigned int activeBufferSize_ = 128;

    const std::string configFilePath_ = "hardware_config.json";
    std::string savedAudioApi_;
    std::string savedAudioDeviceName_;
    unsigned int savedSampleRate_ = 48000;
    unsigned int savedBufferSize_ = 128;
    bool savedAudioMissing_ = false;

    std::set<std::string> savedEnabledMidiPorts_;
    std::set<std::string> savedKnownMidiPorts_;
    std::vector<MidiPortSlot> midiSlots_;
    std::chrono::steady_clock::time_point lastHotPlugCheck_;
    std::mutex hwMutex_;
};

std::string findUiDir() {
    for (const auto& candidate : {"ui", "../ui", "../../ui", "./ui"}) {
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            return candidate;
        }
    }
    return "ui";
}

std::string findUiFile(const std::string& filename) {
    return (fs::path(findUiDir()) / filename).generic_string();
}

json serializeParams(const SynthPresetParams& p) {
    json eqArr = json::array();
    for (int i = 0; i < 5; ++i) {
        eqArr.push_back({
            {"freqHz", p.eqBands[i].freqHz},
            {"q",      p.eqBands[i].q},
            {"gainDb", p.eqBands[i].gainDb}
        });
    }

    json synthLayersArr = json::array();
    for (int i = 0; i < 3; ++i) {
        const auto& c = p.synthLayers[i];
        synthLayersArr.push_back({
            {"enabled", c.enabled},
            {"waveType", c.waveType},
            {"volume", c.volume},
            {"pan", c.pan},
            {"octave", c.octave},
            {"semitones", c.semitones},
            {"detuneCents", c.detuneCents},
            {"cutoffHz", c.cutoffHz},
            {"resonanceQ", c.resonanceQ},
            {"attackSec", c.attackSec},
            {"decaySec", c.decaySec},
            {"sustainLevel", c.sustainLevel},
            {"releaseSec", c.releaseSec},
            {"unisonVoices", c.unisonVoices},
            {"unisonDetune", c.unisonDetune},
            {"unisonSpread", c.unisonSpread},
            {"phaseOffset", c.phaseOffset},
            {"keyTrack", c.keyTrack},
            {"velToCutoff", c.velToCutoff},
            {"velToAmp", c.velToAmp},
            {"wavetableIdx", c.wavetableIdx},
            {"samplePath", c.samplePath},
            {"filterType", c.filterType},
            {"filterEnvAmt", c.filterEnvAmt},
            {"filterKeyTrack", c.filterKeyTrack},
            {"filtAttackSec", c.filtAttackSec},
            {"filtDecaySec", c.filtDecaySec},
            {"filtSustainLvl", c.filtSustainLvl},
            {"filtReleaseSec", c.filtReleaseSec},
            {"sawLevel", c.sawLevel}, {"pulseLevel", c.pulseLevel}, {"pulseWidth", c.pulseWidth}, {"pwmDepth", c.pwmDepth}, {"pwmRateHz", c.pwmRateHz}, {"subLevel", c.subLevel}, {"noiseLevel", c.noiseLevel}, {"osc2Level", c.osc2Level}, {"osc2Detune", c.osc2Detune}, {"osc2Semitones", c.osc2Semitones}, {"osc2Wave", c.osc2Wave}, {"highpassHz", c.highpassHz}, {"drive", c.drive}
        });
    }

    json synthLfoArr = json::array();
    for (const auto& l : p.synthLfos) {
        synthLfoArr.push_back({
            {"enabled", l.enabled}, {"waveType", l.waveType},
            {"rateHz", l.rateHz}, {"depth", l.depth},
            {"delaySec", l.delaySec}, {"fadeSec", l.fadeSec},
            {"destPitch", l.destPitch}, {"destCutoff", l.destCutoff},
            {"destVolume", l.destVolume}, {"destPan", l.destPan},
            {"destPhase", l.destPhase}
        });
    }
    json synthEnvArr = json::array();
    for (const auto& e : p.synthEnvelopes) {
        synthEnvArr.push_back({
            {"delaySec", e.delaySec}, {"attackSec", e.attackSec},
            {"holdSec", e.holdSec}, {"decaySec", e.decaySec},
            {"sustainLevel", e.sustainLevel}, {"releaseSec", e.releaseSec}
        });
    }

    json drawbarsArr = json::array();
    for (int d = 0; d < 9; ++d) {
        drawbarsArr.push_back(p.drawbars[d]);
    }

    return {
        {"pianoEnabled", p.pianoEnabled},
        {"pianoVolume", p.pianoVolume},
        {"pianoLayers", [&] {
            json layers = json::array();
            for (int i = 0; i < PIANO_LAYER_COUNT; ++i)
                layers.push_back({{"enabled", p.pianoLayerEnabled[i]}, {"volume", p.pianoLayerVolume[i]}});
            return layers;
        }()},
        {"pianoSampleConfig", [&] {
            json configs = json::array();
            for (int i = 0; i < PIANO_LAYER_COUNT; ++i) {
                const auto& c = p.pianoLayerSample[i];
                configs.push_back({{"attackSec",c.attackSec},{"decaySec",c.decaySec},
                    {"sustainLevel",c.sustainLevel},{"releaseSec",c.releaseSec},
                    {"loopHold",c.loopHold},{"pan",c.pan},{"fineTuneCents",c.fineTuneCents},
                    {"cutoffHz",c.cutoffHz},{"resonanceQ",c.resonanceQ}});
            }
            return configs;
        }()},
        {"fxSends", [&] {
            json sends = json::array();
            for (int i = 0; i < FX_SOURCE_COUNT; ++i)
                sends.push_back({{"modulation", p.fxSends[i].modulation},
                                 {"delay", p.fxSends[i].delay}, {"reverb", p.fxSends[i].reverb}});
            return sends;
        }()},
        {"maxVoices", p.maxVoices},
        {"transposeSemis", p.transposeSemis},
        {"octaveShift", p.octaveShift},
        {"masterTuneCents", p.masterTuneCents},
        {"softRelease", p.softRelease},
        {"kbdTouch", p.kbdTouch},
        {"cutoffHz", p.cutoffHz},
        {"resonanceQ", p.resonanceQ},
        {"attackSec", p.attackSec},
        {"decaySec", p.decaySec},
        {"sustainLevel", p.sustainLevel},
        {"releaseSec", p.releaseSec},
        {"loopHold", p.loopHold},
        {"volume", p.volume},
        {"pan", p.pan},
        {"eqEnabled", p.eqEnabled},
        {"lowCutHz", p.lowCutHz},
        {"highCutHz", p.highCutHz},
        {"eqBands", eqArr},
        {"chorusEnabled", p.chorusEnabled},
        {"chorusRateHz", p.chorusRateHz},
        {"chorusDepth", p.chorusDepth},
        {"chorusMix", p.chorusMix},
        {"chorusDelay",p.chorusDelay},
        {"chorusTone",p.chorusTone},
        {"chorusFeedback",p.chorusFeedback},
        {"chorusWidth",p.chorusWidth},
        {"chorusVoices",p.chorusVoices},
        {"phaserEnabled", p.phaserEnabled},
        {"phaserRateHz", p.phaserRateHz},
        {"phaserDepth", p.phaserDepth},
        {"phaserFeedback", p.phaserFeedback},
        {"phaserMix", p.phaserMix},
        {"tremoloEnabled", p.tremoloEnabled},
        {"tremoloRateHz", p.tremoloRateHz},
        {"tremoloDepth", p.tremoloDepth},
        {"delayEnabled", p.delayEnabled},
        {"delayTimeMs", p.delayTimeMs},
        {"delayFeedback", p.delayFeedback},
        {"delayDamping", p.delayDamping},
        {"delayMix", p.delayMix},
        {"delayPingPong", p.delayPingPong},
        {"reverbEnabled", p.reverbEnabled},
        {"reverbType", p.reverbType},
        {"reverbMix", p.reverbMix},
        {"reverbSize", p.reverbSize},
        {"reverbDamping", p.reverbDamping},
        {"reverbPreDelayMs", p.reverbPreDelayMs},
        {"reverbShimmer", p.reverbShimmer},
        {"shimmerShiftSt", p.shimmerShiftSt},
        {"shimmerFeedback", p.shimmerFeedback},
        {"shimmerDiffusion", p.shimmerDiffusion},
        {"shimmerLowCutHz", p.shimmerLowCutHz},
        {"shimmerHighCutHz", p.shimmerHighCutHz},
        {"shimmerModRateHz", p.shimmerModRateHz},
        {"shimmerModDepth", p.shimmerModDepth},
        {"shimmerPitchMode", p.shimmerPitchMode},
        {"shimmerReverbMode", p.shimmerReverbMode},
        {"shimmerColorMode", p.shimmerColorMode},
        // Módulo 2: Synth 3-Layer
        {"synthEnabled", p.synthEnabled},
        {"synthPresetId", p.synthPresetId},
        {"synthMasterVol", p.synthMasterVol},
        {"synthMacroCutoff", p.synthMacroCutoff},
        {"synthLayers", synthLayersArr},
        {"synthLfos", synthLfoArr},
        {"synthEnvelopes", synthEnvArr},
        {"synthLfoEnabled", p.synthLfoEnabled},
        {"synthLfoWave", p.synthLfoWave},
        {"synthLfoRate", p.synthLfoRate},
        {"synthLfoDepth", p.synthLfoDepth},
        {"synthLfoPitch", p.synthLfoPitch},
        {"synthLfoCutoff", p.synthLfoCutoff},
        // Módulo 3: Órgano
        {"organEnabled", p.organEnabled},
        {"organModel", p.organModel},
        {"organVolume", p.organVolume},
        {"organRotaryFast", p.organRotaryFast},
        {"organDrive", p.organDrive},
        {"organRotaryMix", p.organRotaryMix},
        {"organVibratoDepth", p.organVibratoDepth},
        {"organVibratoRate", p.organVibratoRate},
        {"organChorusVoices",p.organChorusVoices},
        {"organChorusTone",p.organChorusTone},
        {"organChorusFeedback",p.organChorusFeedback},
        {"organChorusWidth",p.organChorusWidth},
        {"organChorusMix",p.organChorusMix},
        {"organChorusDepth",p.organChorusDepth},
        {"organChorusRate",p.organChorusRate},
        {"organChorusEnabled",p.organChorusEnabled},
        {"drawbars", drawbarsArr},
        {"organVibratoMode", p.organVibratoMode},
        {"organPercEnabled", p.organPercEnabled},
        {"organPercHarmonic", p.organPercHarmonic},
        {"organPercDecaySec", p.organPercDecaySec},
        {"organPercLevel", p.organPercLevel}
    };
}

SynthPresetParams deserializeParams(const json& j, const SynthPresetParams& fallback) {
    SynthPresetParams p = fallback;
    if (!j.is_object()) return p;
    p.pianoEnabled     = j.value("pianoEnabled", p.pianoEnabled);
    p.pianoVolume      = j.value("pianoVolume", p.pianoVolume);
    if (j.contains("pianoLayers") && j["pianoLayers"].is_array()) {
        // Legacy programs had 3 layers; don't leak enabled L4-L5 from current patch.
        for (size_t i = j["pianoLayers"].size(); i < PIANO_LAYER_COUNT; ++i)
            p.pianoLayerEnabled[i] = false;
        for (size_t i = 0; i < std::min<size_t>(PIANO_LAYER_COUNT, j["pianoLayers"].size()); ++i) {
            const auto& layer = j["pianoLayers"][i];
            if (!layer.is_object()) continue;
            p.pianoLayerEnabled[i] = layer.value("enabled", p.pianoLayerEnabled[i]);
            p.pianoLayerVolume[i] = layer.value("volume", p.pianoLayerVolume[i]);
        }
    }
    if (j.contains("fxSends") && j["fxSends"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(FX_SOURCE_COUNT, j["fxSends"].size()); ++i) {
            if (!j["fxSends"][i].is_object()) continue;
            const auto& f = j["fxSends"][i];
            p.fxSends[i].modulation = std::clamp(f.value("modulation", p.fxSends[i].modulation), 0.0f, 1.0f);
            p.fxSends[i].delay = std::clamp(f.value("delay", p.fxSends[i].delay), 0.0f, 1.0f);
            p.fxSends[i].reverb = std::clamp(f.value("reverb", p.fxSends[i].reverb), 0.0f, 1.0f);
        }
    }
    p.maxVoices        = j.value("maxVoices", p.maxVoices);
    p.transposeSemis   = j.value("transposeSemis", p.transposeSemis);
    p.octaveShift      = j.value("octaveShift", p.octaveShift);
    p.masterTuneCents  = j.value("masterTuneCents", p.masterTuneCents);
    p.softRelease      = j.value("softRelease", p.softRelease);
    p.kbdTouch         = j.value("kbdTouch", p.kbdTouch);
    p.cutoffHz         = j.value("cutoffHz", p.cutoffHz);
    p.resonanceQ       = j.value("resonanceQ", p.resonanceQ);
    p.attackSec        = j.value("attackSec", p.attackSec);
    p.decaySec         = j.value("decaySec", p.decaySec);
    p.sustainLevel     = j.value("sustainLevel", p.sustainLevel);
    p.releaseSec       = j.value("releaseSec", p.releaseSec);
    p.loopHold         = j.value("loopHold", p.loopHold);
    // Backwards compatible: old programs without per-layer envelopes use the old global sample envelope.
    if (j.contains("pianoSampleConfig") && j["pianoSampleConfig"].is_array()) {
        for (size_t i=0;i<std::min<size_t>(PIANO_LAYER_COUNT,j["pianoSampleConfig"].size());++i) {
            const auto& c=j["pianoSampleConfig"][i];
            if (!c.is_object()) continue;
            auto& dst=p.pianoLayerSample[i];
            dst.attackSec=c.value("attackSec",dst.attackSec);
            dst.decaySec=c.value("decaySec",dst.decaySec);
            dst.sustainLevel=c.value("sustainLevel",dst.sustainLevel);
            dst.releaseSec=c.value("releaseSec",dst.releaseSec);
            dst.loopHold=c.value("loopHold",dst.loopHold);
            dst.pan=c.value("pan",dst.pan);
            dst.fineTuneCents=c.value("fineTuneCents",dst.fineTuneCents);
            dst.cutoffHz=std::clamp(c.value("cutoffHz",dst.cutoffHz),40.f,20000.f);
            dst.resonanceQ=std::clamp(c.value("resonanceQ",dst.resonanceQ),.5f,12.f);
        }
    } else {
        for (auto& c : p.pianoLayerSample) {
            c.attackSec=std::max(0.005f,p.attackSec);
            c.decaySec=p.decaySec;
            c.sustainLevel=p.sustainLevel;
            c.releaseSec=p.releaseSec;
            c.loopHold=p.loopHold;
            c.pan=0.f;
            c.fineTuneCents=0.f;
        }
    }
    p.volume           = j.value("volume", p.volume);
    p.pan              = j.value("pan", p.pan);

    p.eqEnabled        = j.value("eqEnabled", p.eqEnabled);
    p.lowCutHz         = j.value("lowCutHz", p.lowCutHz);
    p.highCutHz        = j.value("highCutHz", p.highCutHz);
    if (j.contains("eqBands") && j["eqBands"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(5, j["eqBands"].size()); ++i) {
            p.eqBands[i].freqHz = j["eqBands"][i].value("freqHz", p.eqBands[i].freqHz);
            p.eqBands[i].q      = j["eqBands"][i].value("q", p.eqBands[i].q);
            p.eqBands[i].gainDb = j["eqBands"][i].value("gainDb", p.eqBands[i].gainDb);
        }
    }

    p.chorusEnabled    = j.value("chorusEnabled", p.chorusEnabled);
    p.chorusRateHz     = j.value("chorusRateHz", p.chorusRateHz);
    p.chorusDepth      = j.value("chorusDepth", p.chorusDepth);
    p.chorusMix        = j.value("chorusMix", p.chorusMix);
    p.chorusDelay = j.value("chorusDelay", p.chorusDelay);
    p.chorusTone = j.value("chorusTone", p.chorusTone);
    p.chorusFeedback = j.value("chorusFeedback", p.chorusFeedback);
    p.chorusWidth = j.value("chorusWidth", p.chorusWidth);
    p.chorusVoices = j.value("chorusVoices", p.chorusVoices);

    p.phaserEnabled    = j.value("phaserEnabled", p.phaserEnabled);
    p.phaserRateHz     = j.value("phaserRateHz", p.phaserRateHz);
    p.phaserDepth      = j.value("phaserDepth", p.phaserDepth);
    p.phaserFeedback   = j.value("phaserFeedback", p.phaserFeedback);
    p.phaserMix        = j.value("phaserMix", p.phaserMix);

    p.tremoloEnabled   = j.value("tremoloEnabled", p.tremoloEnabled);
    p.tremoloRateHz    = j.value("tremoloRateHz", p.tremoloRateHz);
    p.tremoloDepth     = j.value("tremoloDepth", p.tremoloDepth);

    p.delayEnabled     = j.value("delayEnabled", p.delayEnabled);
    p.delayTimeMs      = j.value("delayTimeMs", p.delayTimeMs);
    p.delayFeedback    = j.value("delayFeedback", p.delayFeedback);
    p.delayDamping     = j.value("delayDamping", p.delayDamping);
    p.delayMix         = j.value("delayMix", p.delayMix);
    p.delayPingPong    = j.value("delayPingPong", p.delayPingPong);

    p.reverbEnabled    = j.value("reverbEnabled", p.reverbEnabled);
    p.reverbType       = j.value("reverbType", p.reverbType);
    p.reverbMix        = j.value("reverbMix", p.reverbMix);
    p.reverbSize       = j.value("reverbSize", p.reverbSize);
    p.reverbDamping    = j.value("reverbDamping", p.reverbDamping);
    p.reverbPreDelayMs = j.value("reverbPreDelayMs", p.reverbPreDelayMs);
    p.reverbShimmer    = j.value("reverbShimmer", p.reverbShimmer);

    p.shimmerShiftSt    = j.value("shimmerShiftSt", p.shimmerShiftSt);
    p.shimmerFeedback   = j.value("shimmerFeedback", p.shimmerFeedback);
    p.shimmerDiffusion  = j.value("shimmerDiffusion", p.shimmerDiffusion);
    p.shimmerLowCutHz   = j.value("shimmerLowCutHz", p.shimmerLowCutHz);
    p.shimmerHighCutHz  = j.value("shimmerHighCutHz", p.shimmerHighCutHz);
    p.shimmerModRateHz  = j.value("shimmerModRateHz", p.shimmerModRateHz);
    p.shimmerModDepth   = j.value("shimmerModDepth", p.shimmerModDepth);
    p.shimmerPitchMode  = j.value("shimmerPitchMode", p.shimmerPitchMode);
    p.shimmerReverbMode = j.value("shimmerReverbMode", p.shimmerReverbMode);
    p.shimmerColorMode  = j.value("shimmerColorMode", p.shimmerColorMode);

    // Módulo 2: Synth 3-Layer
    p.synthEnabled      = j.value("synthEnabled", p.synthEnabled);
    p.synthPresetId     = j.value("synthPresetId", p.synthPresetId);
    p.synthMasterVol    = j.value("synthMasterVol", p.synthMasterVol);
    p.synthMacroCutoff  = j.value("synthMacroCutoff", p.synthMacroCutoff);
    p.synthLfoEnabled = j.value("synthLfoEnabled", p.synthLfoEnabled);
    p.synthLfoWave = j.value("synthLfoWave", p.synthLfoWave);
    p.synthLfoRate = j.value("synthLfoRate", p.synthLfoRate);
    p.synthLfoDepth = j.value("synthLfoDepth", p.synthLfoDepth);
    p.synthLfoPitch = j.value("synthLfoPitch", p.synthLfoPitch);
    p.synthLfoCutoff = j.value("synthLfoCutoff", p.synthLfoCutoff);
    if (j.contains("synthLayers") && j["synthLayers"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(3, j["synthLayers"].size()); ++i) {
            const auto& lp = j["synthLayers"][i];
            auto& c = p.synthLayers[i];
            c.enabled        = lp.value("enabled", c.enabled);
            c.waveType       = lp.value("waveType", c.waveType);
            c.volume         = lp.value("volume", c.volume);
            c.pan            = lp.value("pan", c.pan);
            c.octave         = lp.value("octave", c.octave);
            c.semitones      = lp.value("semitones", c.semitones);
            c.detuneCents    = lp.value("detuneCents", c.detuneCents);
            c.cutoffHz       = lp.value("cutoffHz", c.cutoffHz);
            c.resonanceQ     = lp.value("resonanceQ", c.resonanceQ);
            c.attackSec      = lp.value("attackSec", c.attackSec);
            c.decaySec       = lp.value("decaySec", c.decaySec);
            c.sustainLevel   = lp.value("sustainLevel", c.sustainLevel);
            c.releaseSec     = lp.value("releaseSec", c.releaseSec);
            c.unisonVoices   = lp.value("unisonVoices", c.unisonVoices);
            c.unisonDetune   = lp.value("unisonDetune", c.unisonDetune);
            c.unisonSpread   = lp.value("unisonSpread", c.unisonSpread);
            c.phaseOffset    = lp.value("phaseOffset", c.phaseOffset);
            c.keyTrack       = lp.value("keyTrack", c.keyTrack);
            c.velToCutoff    = lp.value("velToCutoff", c.velToCutoff);
            c.velToAmp       = lp.value("velToAmp", c.velToAmp);
            c.wavetableIdx   = lp.value("wavetableIdx", c.wavetableIdx);
            c.samplePath     = lp.value("samplePath", c.samplePath);
            c.filterType     = lp.value("filterType", c.filterType);
            c.filterEnvAmt   = lp.value("filterEnvAmt", c.filterEnvAmt);
            c.filterKeyTrack = lp.value("filterKeyTrack", c.filterKeyTrack);
            c.filtAttackSec  = lp.value("filtAttackSec", c.filtAttackSec);
            c.filtDecaySec   = lp.value("filtDecaySec", c.filtDecaySec);
            c.filtSustainLvl = lp.value("filtSustainLvl", c.filtSustainLvl);
            c.filtReleaseSec = lp.value("filtReleaseSec", c.filtReleaseSec);
            c.sawLevel = lp.value("sawLevel", c.sawLevel);
            c.pulseLevel = lp.value("pulseLevel", c.pulseLevel);
            c.pulseWidth = lp.value("pulseWidth", c.pulseWidth);
            c.pwmDepth = lp.value("pwmDepth", c.pwmDepth);
            c.pwmRateHz = lp.value("pwmRateHz", c.pwmRateHz);
            c.subLevel = lp.value("subLevel", c.subLevel);
            c.noiseLevel = lp.value("noiseLevel", c.noiseLevel);
            c.osc2Level = lp.value("osc2Level", c.osc2Level);
            c.osc2Detune = lp.value("osc2Detune", c.osc2Detune);
            c.osc2Semitones = lp.value("osc2Semitones", c.osc2Semitones);
            c.osc2Wave = lp.value("osc2Wave", c.osc2Wave);
            c.highpassHz = lp.value("highpassHz", c.highpassHz);
            c.drive = lp.value("drive", c.drive);
        }
    }

    if (j.contains("synthLfos") && j["synthLfos"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(4, j["synthLfos"].size()); ++i) {
            const auto& v = j["synthLfos"][i];
            auto& l = p.synthLfos[i];
            l.enabled = v.value("enabled", l.enabled);
            l.waveType = v.value("waveType", l.waveType);
            l.rateHz = v.value("rateHz", l.rateHz);
            l.depth = v.value("depth", l.depth);
            l.delaySec = v.value("delaySec", l.delaySec);
            l.fadeSec = v.value("fadeSec", l.fadeSec);
            l.destPitch = v.value("destPitch", l.destPitch);
            l.destCutoff = v.value("destCutoff", l.destCutoff);
            l.destVolume = v.value("destVolume", l.destVolume);
            l.destPan = v.value("destPan", l.destPan);
            l.destPhase = v.value("destPhase", l.destPhase);
        }
        // Program bank's old scalar fields remain available for backward compatibility.
        const auto& l = p.synthLfos[0];
        p.synthLfoEnabled = l.enabled;
        p.synthLfoWave = l.waveType;
        p.synthLfoRate = l.rateHz;
        p.synthLfoDepth = l.depth;
        p.synthLfoPitch = l.destPitch;
        p.synthLfoCutoff = l.destCutoff;
    }
    if (j.contains("synthEnvelopes") && j["synthEnvelopes"].is_array()) {
        for (size_t i = 0; i < std::min<size_t>(3, j["synthEnvelopes"].size()); ++i) {
            const auto& v = j["synthEnvelopes"][i];
            auto& e = p.synthEnvelopes[i];
            e.delaySec = v.value("delaySec", e.delaySec);
            e.attackSec = v.value("attackSec", e.attackSec);
            e.holdSec = v.value("holdSec", e.holdSec);
            e.decaySec = v.value("decaySec", e.decaySec);
            e.sustainLevel = v.value("sustainLevel", e.sustainLevel);
            e.releaseSec = v.value("releaseSec", e.releaseSec);
        }
    }

    // Módulo 3: Órgano
    p.organEnabled      = j.value("organEnabled", p.organEnabled);
    p.organModel        = j.value("organModel", p.organModel);
    p.organVolume       = j.value("organVolume", p.organVolume);
    p.organRotaryFast   = j.value("organRotaryFast", p.organRotaryFast);
    p.organDrive        = j.value("organDrive", p.organDrive);
    p.organRotaryMix = j.value("organRotaryMix", p.organRotaryMix);
    p.organVibratoDepth = j.value("organVibratoDepth", p.organVibratoDepth);
    p.organVibratoRate = j.value("organVibratoRate", p.organVibratoRate);
    p.organChorusVoices = j.value("organChorusVoices", p.organChorusVoices);
    p.organChorusTone = j.value("organChorusTone", p.organChorusTone);
    p.organChorusFeedback = j.value("organChorusFeedback", p.organChorusFeedback);
    p.organChorusWidth = j.value("organChorusWidth", p.organChorusWidth);
    p.organChorusMix = j.value("organChorusMix", p.organChorusMix);
    p.organChorusDepth = j.value("organChorusDepth", p.organChorusDepth);
    p.organChorusRate = j.value("organChorusRate", p.organChorusRate);
    p.organChorusEnabled = j.value("organChorusEnabled", p.organChorusEnabled);
    p.organVibratoMode  = j.value("organVibratoMode", p.organVibratoMode);
    p.organPercEnabled  = j.value("organPercEnabled", p.organPercEnabled);
    p.organPercHarmonic = j.value("organPercHarmonic", p.organPercHarmonic);
    p.organPercDecaySec = j.value("organPercDecaySec", p.organPercDecaySec);
    p.organPercLevel    = j.value("organPercLevel", p.organPercLevel);
    if (j.contains("drawbars") && j["drawbars"].is_array()) {
        for (size_t d = 0; d < std::min<size_t>(9, j["drawbars"].size()); ++d) {
            p.drawbars[d] = j["drawbars"][d].get<float>();
        }
    }

    return p;
}

// The piano-layer source paths are stored separately from the realtime DSP parameters.
// This keeps legacy "sourcePath" (layer 1) compatible with older programs.json files.
json serializePianoLayerSources(const NordPianoEngine& engine) {
    json layers = json::array();
    const auto banks = engine.getPianoInstruments();
    for (int i = 0; i < PIANO_LAYER_COUNT; ++i) {
        auto bank = banks[i];
        const std::string path = bank ? fs::path(bank->sourcePath).generic_string() : "";
        layers.push_back({{"layer", i}, {"name", bank ? bank->name : "Sin sonido"},
                          {"sourcePath", path},
                          {"fileName", path.empty() ? "" : fs::path(path).filename().string()},
                          {"instrumentId", bank ? bank->id : 0}});
    }
    return layers;
}

bool restorePianoLayerSources(const json& program, NordPianoEngine& engine,
                              const std::string& libraryRoot, std::string& statusMsg) {
    bool loadedFirst = false;
    const json layers = (program.contains("pianoLayers") && program["pianoLayers"].is_array())
        ? program["pianoLayers"] : json::array();
    for (int i = 0; i < PIANO_LAYER_COUNT; ++i) {
        const json layer = (i < static_cast<int>(layers.size()) && layers[i].is_object())
            ? layers[i] : json::object();
        const std::string savedPath = i == 0 && layer.empty()
            ? program.value("sourcePath", std::string{}) : layer.value("sourcePath", std::string{});
        const std::string filename = i == 0 && layer.empty()
            ? program.value("fileName", std::string{}) : layer.value("fileName", std::string{});
        fs::path resolved(savedPath);
        if ((!fs::exists(resolved) || savedPath.empty()) && !filename.empty()) {
            const fs::path replacement = fs::path(libraryRoot) / filename;
            if (fs::exists(replacement)) resolved = replacement;
        }
        if (savedPath.empty() && filename.empty()) {
            engine.setLayerInstrument(i, {});
            continue;
        }
        if (!fs::exists(resolved) || !fs::is_regular_file(resolved)) {
            statusMsg = "No se encontró el sample de la capa " + std::to_string(i + 1) + ": " + filename;
            engine.setLayerInstrument(i, {}); // prevent a different program's layer from leaking in
            continue;
        }
        auto oldBank = engine.getLayerInstrument(i);
        if (oldBank && fs::path(oldBank->sourcePath) == resolved) {
            if (i == 0) loadedFirst = true;
            continue;
        }
        auto bank = std::make_shared<PianoInstrument>();
        if (BankLoader::loadAny(resolved.generic_string(), *bank, statusMsg)) {
            engine.setLayerInstrument(i, bank);
            if (i == 0) loadedFirst = true;
        } else {
            engine.setLayerInstrument(i, {});
        }
    }
    return loadedFirst;
}

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n";
    std::cout << "   NORD VIRTUAL STAGE 5.1 - STAGE WORKSTATION EDITION    \n";
    std::cout << "=========================================================\n\n";

    WorkstationHost engine;
    MidiOscMapper oscMapper(engine);
    oscMapper.startOscServer(9000);

    std::string libraryRootDir = (argc > 1) ? argv[1] : "./samples";
    if (!fs::exists(libraryRootDir)) libraryRootDir = ".";

    std::string statusMsg = "Listo.";
    std::mutex dataMutex;
    int activeProgramSlot = 0;
    std::string currentProgramName = "Program 1";
    bool initializedEditBuffer = false;
    std::atomic<uint64_t> paramRevCounter{1};

    const std::string programsFilePath = "programs.json";
    json userPrograms = json::array();
    if (fs::exists(programsFilePath)) {
        try {
            std::ifstream pf(programsFilePath);
            userPrograms = json::parse(pf);
        } catch (...) {}
    }

    bool loadedFromProgram1 = false;
    if (userPrograms.is_array() && userPrograms.size() == 8 && userPrograms[0].is_object()) {
        const auto& prog0 = userPrograms[0];
        loadedFromProgram1 = restorePianoLayerSources(prog0, engine, libraryRootDir, statusMsg);
        if (prog0.contains("params")) {
            engine.applyPreset(deserializeParams(prog0["params"], engine.getCurrentParams()));
        }
        // A program may intentionally contain only synth / organ, with no piano.
        if (prog0.value("sourcePath", std::string{}).empty()) loadedFromProgram1 = true;
    }

    if (!loadedFromProgram1) {
        auto initInst = std::make_shared<PianoInstrument>();
        if (BankLoader::loadAny(libraryRootDir, *initInst, statusMsg)) {
            engine.setInstrument(initInst);
            std::cout << "[Inicio] " << statusMsg << "\n";
        }
    }

    if (!userPrograms.is_array() || userPrograms.size() != 8) {
        userPrograms = json::array();
        auto inst = engine.getInstrument();
        std::string normPath = inst ? fs::path(inst->sourcePath).generic_string() : "";
        for (int i = 0; i < 8; ++i) {
            userPrograms.push_back({
                {"slot", i + 1}, {"name", inst ? inst->name : ("Program " + std::to_string(i + 1))},
                {"sourcePath", normPath},
                {"fileName", inst ? fs::path(inst->sourcePath).filename().string() : ""},
                {"pianoLayers", serializePianoLayerSources(engine)},
                {"params", serializeParams(engine.getCurrentParams())}
            });
        }
        std::ofstream pf(programsFilePath);
        if (pf) pf << userPrograms.dump(2);
    }

    currentProgramName = userPrograms[0].value("name", std::string("Program 1"));

    HardwareManager hw(engine);

    const std::string tagsFilePath = "sample_tags.json";
    json sampleTags = json::object();
    if (fs::exists(tagsFilePath)) {
        try {
            std::ifstream tf(tagsFilePath);
            sampleTags = json::parse(tf);
        } catch (...) {}
    }
    if (!sampleTags.is_object()) sampleTags = json::object();

    httplib::Server svr;

    // Montar carpeta ui/ para servir CSS (/css/styles.css), JS (/js/core.js) y páginas HTML
    std::string uiRootDir = findUiDir();
    svr.set_mount_point("/", uiRootDir);
    svr.set_mount_point("/ui", uiRootDir);

    auto serveHtmlPage = [](const std::string& file, httplib::Response& res) {
        std::ifstream f(findUiFile(file), std::ios::binary);
        if (!f) {
            res.status = 404;
            res.set_content("<h1>Error: No se encontro " + file + "</h1>", "text/html; charset=utf-8");
            return;
        }
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        res.set_content(content, "text/html; charset=utf-8");
    };

    svr.Get("/", [&](const httplib::Request&, httplib::Response& res) { serveHtmlPage("index.html", res); });
    svr.Get("/synth", [&](const httplib::Request&, httplib::Response& res) {
        serveHtmlPage("synth.html", res);
        res.set_header("Cache-Control", "no-store, max-age=0");
    });
    svr.Get("/organ", [&](const httplib::Request&, httplib::Response& res) { serveHtmlPage("organ.html", res); });
    svr.Get("/settings", [&](const httplib::Request&, httplib::Response& res) { serveHtmlPage("settings.html", res); });

    svr.Get("/api/state", [&](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store, max-age=0");
        hw.pollHotPlugIfNeeded();
        SynthPresetParams p = engine.getCurrentParams();
        auto inst = engine.getInstrument();

        std::array<uint8_t, 128> ks;
        engine.getActiveKeyStates(ks);
        json activeKeysJson = json::array();
        for (int k = 21; k <= 108; ++k) {
            if (ks[k] != 0) {
                activeKeysJson.push_back({{"k", k}, {"s", ks[k]}});
            }
        }

        auto hwJson = hw.getHardwareStatusJson();

        json j = {
            {"activeProgramSlot", activeProgramSlot},
            {"programName", currentProgramName},
            {"paramRev", paramRevCounter.load()},
            {"pianoLayers", serializePianoLayerSources(engine)},
            {"instrumentId", inst ? inst->id : 0},
            {"instrumentName", inst ? inst->name : "Synth / Organ Stage"},
            {"sourcePath", inst ? fs::path(inst->sourcePath).generic_string() : ""},
            {"formatType", inst ? inst->formatType : "NONE"},
            {"sampleCount", inst ? inst->strokes.size() : 0},
            {"zoneCount", inst ? inst->zoneCount : 0},
            {"memoryMB", inst ? inst->memoryMB : 0.0},
            {"statusMessage", statusMsg},
            {"libraryRoot", libraryRootDir},
            {"activeVoices", engine.getActiveVoiceCount()},
            {"maxVoices", engine.getMaxVoices()},
            {"peakL", engine.getPeakLeft()},
            {"peakR", engine.getPeakRight()},
            {"clipping", engine.isClipping()},
            {"pedalDown", engine.isPedalDown()},
            {"lastMidiNote", engine.getLastMidiNote()},
            {"lastMidiVel", engine.getLastMidiVel()},
            {"lastStrokeIdx", engine.getLastStrokeIndex()},
            {"midiSeq", engine.getMidiEventSeq()},
            {"modWheel",engine.getModWheel()},
            {"pitchBend",engine.getPitchBend()},
            {"activeKeys", activeKeysJson},
            {"connectedMidiCount", hwJson["connectedEnabledMidiCount"]},
            {"disconnectedMidiCount", hwJson["disconnectedEnabledMidiCount"]},
            {"savedAudioMissing", hwJson["savedAudioMissing"]},
            {"params", serializeParams(p)}
        };
        res.set_content(j.dump(), "application/json");
    });

    svr.Get("/api/programs", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        res.set_content(userPrograms.dump(), "application/json");
    });

    svr.Post("/api/programs/store", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        int slotIdx = std::clamp(body.value("slot", 0), 0, 7);
        activeProgramSlot = slotIdx;

        auto inst = engine.getInstrument();
        std::string normPath = inst ? fs::path(inst->sourcePath).generic_string() : "";
        std::string fName = inst ? fs::path(inst->sourcePath).filename().string() : "";
        std::string suggested = inst ? inst->name : ("Program " + std::to_string(slotIdx + 1));
        std::string instName = body.value("name", suggested);
        // Trim/limit user-supplied display names (no HTML injection; UI uses textContent).
        const auto begin = instName.find_first_not_of(" \t\n\r");
        if (begin == std::string::npos) instName = suggested;
        else { const auto end = instName.find_last_not_of(" \t\n\r");
            instName = instName.substr(begin, end - begin + 1); }
        if (instName.size() > 48) instName.resize(48);
        currentProgramName = instName;
        initializedEditBuffer = false;

        userPrograms[slotIdx] = {
            {"slot", slotIdx + 1},
            {"name", instName},
            {"sourcePath", normPath},
            {"fileName", fName},
            {"pianoLayers", serializePianoLayerSources(engine)},
            {"params", serializeParams(engine.getCurrentParams())}
        };
        std::ofstream pf(programsFilePath);
        if (pf) pf << userPrograms.dump(2);

        res.set_content(json({
            {"ok", true},
            {"slot", slotIdx},
            {"storedName", instName},
            {"programName", currentProgramName},
            {"programs", userPrograms}
        }).dump(), "application/json");
    });

    svr.Post("/api/programs/recall", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        int slotIdx = std::clamp(body.value("slot", 0), 0, 7);
        activeProgramSlot = slotIdx;
        const auto& prog = userPrograms[slotIdx];
        currentProgramName = prog.value("name", "Program");
        initializedEditBuffer = false;

        restorePianoLayerSources(prog, engine, libraryRootDir, statusMsg);
        if (prog.contains("params")) {
            SynthPresetParams p = deserializeParams(prog["params"], engine.getCurrentParams());
            engine.applyPreset(p);
        }
        paramRevCounter.fetch_add(1);

        auto activeInst = engine.getInstrument();
        res.set_content(json({
            {"ok", true},
            {"slot", slotIdx},
            {"programName", currentProgramName},
            {"pianoLayers", serializePianoLayerSources(engine)},
            {"instrumentId", activeInst ? activeInst->id : 0},
            {"instrumentName", activeInst ? activeInst->name : prog.value("name", "Program")},
            {"sourcePath", activeInst ? fs::path(activeInst->sourcePath).generic_string() : ""},
            {"zoneCount", activeInst ? activeInst->zoneCount : 0},
            {"memoryMB", activeInst ? activeInst->memoryMB : 0.0},
            {"params", serializeParams(engine.getCurrentParams())}
        }).dump(), "application/json");
    });

    // INIT resets only the active edit buffer; it DOES NOT overwrite programs.json.
    // The player explicitly stores the initialized setup with STORE if desired.
    svr.Post("/api/programs/init", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        engine.allNotesOff();
        for (int i = 0; i < PIANO_LAYER_COUNT; ++i) engine.setLayerInstrument(i, {});
        engine.applyPreset(SynthPresetParams{});
        currentProgramName = "INIT PERFORMANCE";
        initializedEditBuffer = true;
        paramRevCounter.fetch_add(1);
        res.set_content(json({{"ok",true}, {"programName",currentProgramName},
            {"slot",activeProgramSlot}, {"pianoLayers",serializePianoLayerSources(engine)},
            {"params",serializeParams(engine.getCurrentParams())}}).dump(), "application/json");
    });

    svr.Post("/api/programs/import", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto arr = json::parse(req.body, nullptr, false);
        if (arr.is_array() && arr.size() == 8) {
            userPrograms = arr;
            std::ofstream pf(programsFilePath);
            if (pf) pf << userPrograms.dump(2);
            res.set_content(json({{"ok", true}, {"programs", userPrograms}}).dump(), "application/json");
            return;
        }
        res.set_content("{\"ok\":false}", "application/json");
    });

    svr.Get("/api/library", [&](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("dir")) {
            std::string reqDir = req.get_param_value("dir");
            if (!reqDir.empty()) libraryRootDir = reqDir;
        }
        auto items = BankLoader::scanLibraryDirectory(libraryRootDir);
        std::lock_guard<std::mutex> lock(dataMutex);

        json arr = json::array();
        for (const auto& it : items) {
            std::string group = it.instrumentGroup;
            // Non-destructive filename heuristics. Explicit user tags override these defaults.
            const auto lowered = [&]{ std::string name=it.name; std::transform(name.begin(),name.end(),name.begin(),
                [](unsigned char c){return static_cast<char>(std::tolower(c));}); return name; }();
            const auto has = [&](const char* token){return lowered.find(token)!=std::string::npos;};
            if (has("bell") || has("chime")) group="Bells & Chimes";
            else if (has("pad") || has("ambient") || has("atmos")) group="Pads & Atmospheres";
            else if (has("lead")) group="Synth Leads";
            else if (has("bass") || has("sub")) group="Bass & Sub";
            else if (has("pluck")) group="Plucks & Arpeggios";
            else if (has("brass") || has("horn") || has("trumpet")) group="Brass";
            else if (has("string") || has("violin") || has("cello")) group="Strings";
            else if (has("choir") || has("vox") || has("vocal")) group="Choirs & Vocals";
            else if (has("guitar") || has("ukulele")) group="Guitars & Plucked";
            else if (has("drum") || has("percussion") || has("perc")) group="Drums & Percussion";
            else if (has("rhodes") || has("wurlitzer") || has("clav")) group="Electric Pianos";
            bool favorite = false;
            if (sampleTags.contains(it.name) && sampleTags[it.name].is_object()) {
                group = sampleTags[it.name].value("group", group);
                favorite = sampleTags[it.name].value("favorite", false);
            }
            arr.push_back({
                {"name", it.name},
                {"path", it.path},
                {"ext", it.extension},
                {"category", it.category},
                {"group", group},
                {"favorite", favorite},
                {"sizeMB", static_cast<double>(it.sizeBytes) / (1024.0 * 1024.0)},
                {"ready", it.hasAudioReady}
            });
        }
        res.set_content(json({{"rootDir", libraryRootDir}, {"items", arr}}).dump(), "application/json");
    });

    svr.Post("/api/library/tag", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto b = json::parse(req.body, nullptr, false);
        std::string fileName = b.value("name", "");
        if (!fileName.empty()) {
            if (!sampleTags.contains(fileName) || !sampleTags[fileName].is_object()) {
                sampleTags[fileName] = json::object();
            }
            if (b.contains("group")) sampleTags[fileName]["group"] = b.value("group", "Acoustic Pianos");
            if (b.contains("favorite")) sampleTags[fileName]["favorite"] = b.value("favorite", false);
            std::ofstream tf(tagsFilePath);
            if (tf) tf << sampleTags.dump(2);
        }
        res.set_content("{\"ok\":true}", "application/json");
    });

    svr.Get("/api/hardware", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(hw.getHardwareStatusJson().dump(), "application/json");
    });

    svr.Post("/api/hardware/rescan", [&](const httplib::Request&, httplib::Response& res) {
        hw.scanAllAudioDevices();
        hw.restoreOrInitAudio();
        hw.refreshMidiPorts();
        auto st = hw.getHardwareStatusJson();
        st["message"] = "Hardware re-escaneado y configuracion verificada.";
        res.set_content(st.dump(), "application/json");
    });

    svr.Post("/api/hardware/audio", [&](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        int uid = body.value("uid", -1);
        unsigned int sr = body.value("sampleRate", 0u);
        unsigned int bufSize = body.value("bufferSize", 128u);
        std::string msg;
        bool ok = hw.configureAudio(uid, sr, bufSize, msg);
        auto st = hw.getHardwareStatusJson();
        st["ok"] = ok;
        st["message"] = msg;
        res.set_content(st.dump(), "application/json");
    });

    svr.Post("/api/hardware/midi", [&](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (body.value("refresh", false)) {
            hw.refreshMidiPorts();
        } else if (body.contains("forgetName")) {
            hw.forgetDisconnectedMidiPort(body.value("forgetName", ""));
        } else {
            std::string portName = body.value("name", "");
            bool enabled = body.value("enabled", false);
            if (!portName.empty()) {
                hw.setMidiPortEnabledByName(portName, enabled);
            } else if (body.contains("index")) {
                hw.setMidiPortEnabledByIndex(body.value("index", -1), enabled);
            }
        }
        res.set_content(hw.getHardwareStatusJson().dump(), "application/json");
    });

    svr.Post("/api/load", [&](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        std::string targetPath = body.value("path", "");
        int pianoLayer = std::clamp(body.value("layer", 0), 0, PIANO_LAYER_COUNT - 1);

        fs::path p(targetPath);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool ok = false;
        SynthPresetParams keepParams = engine.getCurrentParams();
        if (ext == ".ns3f" || ext == ".ns2p" || ext == ".ns2f" || ext == ".nw2p") {
            PianoInstrument temp;
            ok = BankLoader::loadClaviaProgramPreset(targetPath, temp, statusMsg);
            if (ok) engine.applyPreset(temp.defaultPreset);
        } else {
            auto newInst = std::make_shared<PianoInstrument>();
            ok = BankLoader::loadAny(targetPath, *newInst, statusMsg);
            if (ok) {
                engine.setLayerInstrument(pianoLayer, newInst);
                engine.setPianoLayerEnabled(pianoLayer, true);
                engine.setMaxVoices(keepParams.maxVoices);
            }
        }
        paramRevCounter.fetch_add(1);

        auto activeInst = engine.getInstrument();
        res.set_content(json({
            {"ok", ok},
            {"layer", pianoLayer},
            {"pianoLayers", serializePianoLayerSources(engine)},
            {"message", statusMsg},
            {"instrumentId", activeInst ? activeInst->id : 0},
            {"instrumentName", activeInst ? activeInst->name : "No Sound Loaded"},
            {"sourcePath", activeInst ? fs::path(activeInst->sourcePath).generic_string() : ""},
            {"formatType", activeInst ? activeInst->formatType : "NONE"},
            {"sampleCount", activeInst ? activeInst->strokes.size() : 0},
            {"zoneCount", activeInst ? activeInst->zoneCount : 0},
            {"memoryMB", activeInst ? activeInst->memoryMB : 0.0},
            {"params", serializeParams(engine.getCurrentParams())}
        }).dump(), "application/json");
    });

    svr.Post("/api/piano/layers/clear", [&](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (!body.is_object() || !body.contains("layer") || !body["layer"].is_number_integer()
            || body["layer"].get<int>() < 0 || body["layer"].get<int>() >= PIANO_LAYER_COUNT) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Capa inválida"})", "application/json");
            return;
        }
        const int layer = body["layer"].get<int>();
        engine.setLayerInstrument(layer, {});
        engine.setPianoLayerEnabled(layer, false);
        paramRevCounter.fetch_add(1);
        res.set_content(json({{"ok", true}, {"pianoLayers", serializePianoLayerSources(engine)},
                              {"params", serializeParams(engine.getCurrentParams())}}).dump(), "application/json");
    });

    // Factory patches: explicit runtime/version diagnostics. A running copy of the
    // previous executable can still own :8080 after the new binary was compiled.
    // The browser should never claim a factory patch was loaded based only on the
    // HTTP POST; this GET reads the actual active engine parameters.
    constexpr int kFactoryPresetCount = 48;
    const std::string factoryBuild = "Live Stage V6.2 / Factory API 2";
    auto factoryState = [&]() {
        const auto p = engine.getCurrentParams();
        json layerTimbres = json::array();
        int enabledLayers = 0;
        for (const auto& layer : p.synthLayers) {
            if (layer.enabled) ++enabledLayers;
            layerTimbres.push_back({
                {"enabled", layer.enabled}, {"wave", layer.waveType},
                {"cutoff", layer.cutoffHz}, {"attack", layer.attackSec},
                {"volume", layer.volume}
            });
        }
        return json({
            {"ok", true}, {"build", factoryBuild},
            {"apiVersion", 2}, {"maxFactoryPresets", kFactoryPresetCount},
            {"presetId", p.synthPresetId}, {"synthEnabled", p.synthEnabled},
            {"synthMasterVol", p.synthMasterVol},
            {"enabledLayers", enabledLayers}, {"layerTimbres", layerTimbres},
            {"uiRoot", findUiDir()}
        });
    };
    svr.Get("/api/synth/factory/status", [&](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store, max-age=0");
        res.set_content(factoryState().dump(), "application/json");
    });

    // Factory synth patches are independent from the 8 stored performance programs.
    // One endpoint avoids a two-request race (power ON, then preset selection).
    svr.Post("/api/synth/factory/load", [&](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        if (!body.is_object() || !body.contains("id") || !body["id"].is_number_integer()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Se requiere un id entero de 1 a 48"})", "application/json");
            return;
        }
        const int id = body["id"].get<int>();
        if (id < 1 || id > kFactoryPresetCount) {
            res.status = 400;
            // Observa la palabra "json" añadida como delimitador
            res.set_content(R"json({"ok":false,"error":"Preset fuera de rango (1 a 48)"})json", "application/json");
            return;
        }
        const bool changed = engine.applyParamById("synthPresetId", static_cast<float>(id), false);
        const bool powered = engine.applyParamById("synthEnabled", 1.0f, false);
        if (!changed || !powered) {
            res.status = 500;
            res.set_content(R"({"ok":false,"error":"No fue posible activar el sintetizador o cargar el preset"})", "application/json");
            return;
        }
        paramRevCounter.fetch_add(1);
        const auto params = engine.getCurrentParams();
        const bool active = (params.synthPresetId == id && params.synthEnabled);
        auto diagnostic = factoryState();
        diagnostic["ok"] = active;
        diagnostic["params"] = serializeParams(params);
        res.set_header("Cache-Control", "no-store, max-age=0");
        res.set_content(diagnostic.dump(), "application/json");
    });

    svr.Post("/api/param", [&](const httplib::Request& req, httplib::Response& res) {
        auto b = json::parse(req.body, nullptr, false);
        std::string id = b.value("id", "");
        float val = b.value("value", 0.0f);

        bool applied = engine.applyParamById(id, val, false);
        paramRevCounter.fetch_add(1);

        if (id == "synthPresetId") {
            res.set_content(json({
                {"ok", applied},
                {"params", serializeParams(engine.getCurrentParams())}
            }).dump(), "application/json");
            return;
        }
        res.set_content(json({{"ok", applied}}).dump(), "application/json");
    });

    svr.Post("/api/note", [&](const httplib::Request& req, httplib::Response& res) {
        auto b = json::parse(req.body, nullptr, false);
        int key = b.value("key", 60);
        int vel = b.value("vel", 100);
        bool on = b.value("on", true);
        if (on) engine.noteOn(key, vel);
        else engine.noteOff(key);
        res.set_content("{\"ok\":true}", "application/json");
    });

    svr.Post("/api/testtone", [&](const httplib::Request&, httplib::Response& res) {
        engine.triggerTestTone();
        res.set_content("{\"ok\":true}", "application/json");
    });

    // Presets dedicados de Sintetizador y Órgano (unificados sin duplicidad)
    const std::string synthPresetsPath = "synth_presets.json";
    const std::string organPresetsPath = "organ_presets.json";

    auto loadPresetsFile = [](const std::string& path) {
        json arr = json::array();
        if (fs::exists(path)) {
            try {
                std::ifstream f(path);
                arr = json::parse(f);
            } catch (...) {}
        }
        return arr;
    };

    auto savePresetsFile = [](const std::string& path, const json& arr) {
        std::ofstream f(path);
        if (f) f << arr.dump(2);
    };

    svr.Get("/api/synth/presets", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        res.set_content(loadPresetsFile(synthPresetsPath).dump(), "application/json");
    });

    svr.Post("/api/synth/presets/save", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string name = body.value("name", "Untitled");
        json presets = loadPresetsFile(synthPresetsPath);
        json newPreset = {
            {"name", name},
            {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()},
            {"params", {
                {"synthEnabled", engine.getSynthEnabled()},
                {"synthMasterVol", engine.getSynthMasterVol()},
                {"synthMacroCutoff", engine.getSynthMacroCutoff()},
                {"layers", engine.getSynthLayersJson()},
                {"lfos", engine.getSynthLFOsJson()},
                {"envelopes", engine.getSynthEnvelopesJson()}
            }}
        };
        bool replaced = false;
        for (auto& p : presets) {
            if (p.value("name", "") == name) { p = newPreset; replaced = true; break; }
        }
        if (!replaced) presets.push_back(newPreset);
        savePresetsFile(synthPresetsPath, presets);
        res.set_content(json({{"ok", true}, {"presets", presets}}).dump(), "application/json");
    });

    svr.Post("/api/synth/presets/load", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string name = body.value("name", "");
        json presets = loadPresetsFile(synthPresetsPath);
        for (const auto& p : presets) {
            if (p.value("name", "") == name) {
                const json& pData = p.contains("params") ? p["params"] : p;
                engine.applySynthPresetJson(pData);
                paramRevCounter.fetch_add(1);
                res.set_content(json({
                    {"ok", true},
                    {"preset", p},
                    {"params", serializeParams(engine.getCurrentParams())}
                }).dump(), "application/json");
                return;
            }
        }
        res.set_content("{\"ok\":false,\"error\":\"Preset no encontrado\"}", "application/json");
    });

    svr.Post("/api/synth/presets/delete", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string name = body.value("name", "");
        json presets = loadPresetsFile(synthPresetsPath);
        presets.erase(std::remove_if(presets.begin(), presets.end(),
            [&](const json& p) { return p.value("name", "") == name; }), presets.end());
        savePresetsFile(synthPresetsPath, presets);
        res.set_content(json({{"ok", true}, {"presets", presets}}).dump(), "application/json");
    });

    svr.Get("/api/organ/presets", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        res.set_content(loadPresetsFile(organPresetsPath).dump(), "application/json");
    });

    svr.Post("/api/organ/presets/save", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string name = body.value("name", "Untitled");
        json presets = loadPresetsFile(organPresetsPath);
        json newPreset = {
            {"name", name},
            {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()},
            {"params", engine.getOrganParamsJson()}
        };
        bool replaced = false;
        for (auto& p : presets) {
            if (p.value("name", "") == name) { p = newPreset; replaced = true; break; }
        }
        if (!replaced) presets.push_back(newPreset);
        savePresetsFile(organPresetsPath, presets);
        res.set_content(json({{"ok", true}, {"presets", presets}}).dump(), "application/json");
    });

    svr.Post("/api/organ/presets/load", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string name = body.value("name", "");
        json presets = loadPresetsFile(organPresetsPath);
        for (const auto& p : presets) {
            if (p.value("name", "") == name) {
                const json& pData = p.contains("params") ? p["params"] : p;
                engine.applyOrganPresetJson(pData);
                paramRevCounter.fetch_add(1);
                res.set_content(json({
                    {"ok", true},
                    {"preset", p},
                    {"params", serializeParams(engine.getCurrentParams())}
                }).dump(), "application/json");
                return;
            }
        }
        res.set_content("{\"ok\":false,\"error\":\"Preset no encontrado\"}", "application/json");
    });

    svr.Post("/api/organ/presets/delete", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string name = body.value("name", "");
        json presets = loadPresetsFile(organPresetsPath);
        presets.erase(std::remove_if(presets.begin(), presets.end(),
            [&](const json& p) { return p.value("name", "") == name; }), presets.end());
        savePresetsFile(organPresetsPath, presets);
        res.set_content(json({{"ok", true}, {"presets", presets}}).dump(), "application/json");
    });

    svr.Get("/api/synth/wavetables", [&](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (const auto& wt : engine.getWavetableBank()) {
            arr.push_back({{"name", wt.name}, {"custom", wt.custom}, {"size", SYNTH_WAVETABLE_SIZE}});
        }
        res.set_content(arr.dump(), "application/json");
    });

    svr.Post("/api/synth/wavetables/create", [&](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        Wavetable wt;
        wt.name = body.value("name", "Custom");
        wt.custom = true;
        if (body.contains("data") && body["data"].is_array()) {
            for (size_t i = 0; i < SYNTH_WAVETABLE_SIZE && i < body["data"].size(); ++i)
                wt.data[i] = std::clamp(body["data"][i].get<float>(), -1.0f, 1.0f);
        }
        engine.addWavetable(wt);
        res.set_content("{\"ok\":true}", "application/json");
    });

    svr.Post("/api/organ/loadsample", [&](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        std::string path = body.value("path", "");
        bool ok = engine.loadOrganSample(path);
        res.set_content(json({{"ok", ok}, {"path", path}}).dump(), "application/json");
    });

    std::cout << "\n[GUI] Nord Virtual Stage: http://localhost:8080\n";
#if defined(_WIN32)
    std::system("start http://localhost:8080");
#elif defined(__APPLE__)
    std::system("open http://localhost:8080");
#endif

    svr.listen("0.0.0.0", 8080);
    return 0;
}