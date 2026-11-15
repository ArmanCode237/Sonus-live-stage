#pragma once
// Audio/MIDI device discovery, persistence and hot-plug (RtAudio / RtMidi).
// Header-only: every method is defined inline in the class body.
#include "app/AppCommon.h"
#include "app/AudioCallbacks.h"
#include "app/UserDataPaths.h"
#include "core/WorkstationHost.h"
#include <RtAudio.h>
#include <RtMidi.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

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
        audioProbeErrors_.clear();
        activeUid_ = -1;
        activeDeviceName_ = "None";

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
            } catch (const std::exception& e) {
                audioProbeErrors_.push_back(apiLabel + ": " + e.what());
                std::cerr << "[Audio/PROBE] " << audioProbeErrors_.back() << "\n";
            } catch (...) {
                audioProbeErrors_.push_back(apiLabel + ": error al enumerar dispositivos");
            }
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
                lastAudioError_.clear();
                return;
            }
            savedAudioMissing_ = true;
            if (foundUid < 0 && savedAudioApi_ == "ASIO" &&
                std::find(availableApis_.begin(), availableApis_.end(), "ASIO") == availableApis_.end()) {
                lastAudioError_ = "ASIO no fue compilado en esta versión. Recompila con STAGE_ENABLE_ASIO=ON.";
            } else if (foundUid < 0) {
                lastAudioError_ = "No aparece el dispositivo guardado [" + savedAudioApi_ + "] " + savedAudioDeviceName_ + ". Verifica el driver y reescanea.";
            } else if (!msg.empty()) {
                lastAudioError_ = msg;
            }
            std::cerr << "[Audio] " << lastAudioError_ << " Usando salida de respaldo...\n";
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
            lastAudioError_.clear();
            saveConfigInternal();
            return true;
        }

        std::string failReason = outMsg;
        std::string restoreMsg;
        const bool restored = tryOpenInternal(prevUid, prevSr, prevBuf, restoreMsg);
        if (!restored) {
            std::string fallbackMsg;
            tryOpenInternal(-1, 0, 256, fallbackMsg);
        }
        lastAudioError_ = failReason;
        outMsg = failReason + (restored ? " (Se restauró la salida activa anterior)." : " (Se intentó activar la salida de respaldo).");
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
        j["asioCompiled"] = std::find(availableApis_.begin(), availableApis_.end(), "ASIO") != availableApis_.end();
        j["asioDeviceCount"] = std::count_if(devices_.begin(), devices_.end(),
            [](const AudioDeviceSpec& d){ return d.apiName == "ASIO"; });
        j["audioProbeErrors"] = audioProbeErrors_;
        j["lastAudioError"] = lastAudioError_;
        j["activeApi"] = activeApiName_;
        j["activeAudioUid"] = activeUid_;
        j["activeAudioDeviceName"] = activeDeviceName_;
        j["sampleRate"] = activeSampleRate_;
        j["bufferSize"] = activeBufferSize_;
        j["latencyMs"] = (activeSampleRate_ > 0) ? ((static_cast<double>(activeBufferSize_) * 1000.0) / activeSampleRate_) : 0.0;
        j["audioUnderflows"] = engine_.getAudioUnderflows();
        j["audioOverBudgetCallbacks"] = engine_.getAudioOverBudgetCallbacks();
        j["maxAudioRenderMicros"] = engine_.getMaxAudioRenderMicros();
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
        // Only an explicit request must select an exact device; never silently
        // route a failed ASIO selection to another interface.
        if (!target && uid >= 0) {
            outMsg = "El dispositivo solicitado ya no existe. Reescanea el hardware.";
            return false;
        }
        if (!target) {
            for (const auto& d : devices_) {
                if (d.isDefault) { target = &d; break; }
            }
        }
        if (!target && !devices_.empty()) target = &devices_[0];
        if (!target) {
            outMsg = "No se encontraron salidas de audio. Comprueba el driver y la conexión USB.";
            return false;
        }

        stopAudioInternal();
        const bool isAsio = (target->api == RtAudio::WINDOWS_ASIO);
        // RtAudio may coerce ASIO bufferFrames to the size required by the
        // driver.  Start with the user's choice; retry common stable sizes.
        const unsigned int requested = requestedBuffer >= 32 ? requestedBuffer : 256;
        std::vector<unsigned int> candidates;
        auto pushBuffer = [&](unsigned int value) {
            if (std::find(candidates.begin(), candidates.end(), value) == candidates.end())
                candidates.push_back(value);
        };
        pushBuffer(requested);
        if (isAsio) { pushBuffer(256); pushBuffer(512); pushBuffer(128); }

        unsigned int sr = requestedSr ? requestedSr : target->preferredSr;
        if (!sr) sr = 48000;
        if (!target->supportedSampleRates.empty() &&
            std::find(target->supportedSampleRates.begin(), target->supportedSampleRates.end(), sr) == target->supportedSampleRates.end()) {
            outMsg = "El dispositivo [" + target->apiName + "] " + target->name +
                " no admite " + std::to_string(sr) + " Hz. Selecciona una frecuencia compatible.";
            return false;
        }

        std::string errors;
        for (const unsigned int candidate : candidates) {
            stopAudioInternal();
            try {
                dac_ = std::make_unique<RtAudio>(target->api);
                dac_->showWarnings(false);
                RtAudio::StreamParameters outParams;
                outParams.deviceId = target->rtDeviceId;
                outParams.nChannels = 2;
                outParams.firstChannel = 0;
                RtAudio::StreamOptions options;
                options.flags = RTAUDIO_SCHEDULE_REALTIME;
                options.streamName = "Live Stage";
                unsigned int bufFrames = candidate;

                const RtAudioErrorType opened = dac_->openStream(&outParams, nullptr,
                    RTAUDIO_FLOAT32, sr, &bufFrames, &audioCallback, &engine_, &options);
                if (opened != RTAUDIO_NO_ERROR) {
                    errors = dac_->getErrorText();
                    if (errors.empty()) errors = "openStream devolvió error " + std::to_string(static_cast<int>(opened));
                    std::cerr << "[Audio/OPEN] [" << target->apiName << "] " << target->name
                              << " @ " << sr << " Hz, " << candidate << " samples: " << errors << "\n";
                    continue;
                }
                // The audio callback begins running during startStream(): the
                // engine must have the driver's *actual* sample rate first.
                unsigned int actualSr = dac_->getStreamSampleRate();
                if (!actualSr) actualSr = sr;
                engine_.setSampleRate(actualSr);
                const RtAudioErrorType started = dac_->startStream();
                if (started != RTAUDIO_NO_ERROR) {
                    errors = dac_->getErrorText();
                    if (errors.empty()) errors = "startStream devolvió error " + std::to_string(static_cast<int>(started));
                    std::cerr << "[Audio/START] [" << target->apiName << "] " << target->name
                              << " @ " << actualSr << " Hz, " << bufFrames << " samples: " << errors << "\n";
                    continue;
                }
                activeUid_ = target->uid;
                activeApiName_ = target->apiName;
                activeDeviceName_ = target->name;
                activeSampleRate_ = actualSr;
                activeBufferSize_ = bufFrames;
                outMsg = "Activo: [" + activeApiName_ + "] " + activeDeviceName_ +
                    " @ " + std::to_string(actualSr) + " Hz (" + std::to_string(bufFrames) + " samples)";
                if (candidate != requested)
                    outMsg += ". Buffer ajustado automáticamente para el driver (solicitado " + std::to_string(requested) + ").";
                std::cout << "[Audio] " << outMsg << "\n";
                return true;
            } catch (const std::exception& e) {
                errors = e.what();
                std::cerr << "[Audio/OPEN] " << errors << "\n";
            } catch (...) {
                errors = "Excepción no identificada de la API de audio";
            }
        }
        stopAudioInternal();
        outMsg = "No se pudo abrir [" + target->apiName + "] " + target->name +
            " @ " + std::to_string(sr) + " Hz: " + (errors.empty() ? "error desconocido" : errors);
        if (isAsio) outMsg += ". Comprueba el driver ASIO nativo, su panel de control y que otro DAW no tenga el dispositivo ocupado.";
        lastAudioError_ = outMsg;
        return false;
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
    std::vector<std::string> audioProbeErrors_;
    std::string lastAudioError_;
    std::vector<AudioDeviceSpec> devices_;
    int activeUid_ = -1;
    std::string activeApiName_ = "WASAPI";
    std::string activeDeviceName_ = "None";
    unsigned int activeSampleRate_ = 48000;
    unsigned int activeBufferSize_ = 128;

    const std::string configFilePath_ = stageUserFile("hardware_config.json");
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
