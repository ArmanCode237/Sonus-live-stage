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

using namespace NordDSP;
using json = nlohmann::json;
namespace fs = std::filesystem;

int audioCallback(void* outputBuffer, void* /*inputBuffer*/, unsigned int nBufferFrames,
                  double /*streamTime*/, RtAudioStreamStatus /*status*/, void* userData) {
    auto* engine = static_cast<NordPianoEngine*>(userData);
    auto* out = static_cast<float*>(outputBuffer);
    engine->render(out, nBufferFrames);
    return 0;
}

void midiCallback(double /*deltatime*/, std::vector<unsigned char>* message, void* userData) {
    if (!message || message->size() < 2) return;
    auto* engine = static_cast<NordPianoEngine*>(userData);

    unsigned char status = (*message)[0] & 0xF0;
    unsigned char data1  = (*message)[1] & 0x7F;
    unsigned char data2  = (message->size() > 2) ? ((*message)[2] & 0x7F) : 0;

    if (status == 0x90 && data2 > 0) {
        engine->noteOn(data1, data2);
    } else if (status == 0x80 || (status == 0x90 && data2 == 0)) {
        engine->noteOff(data1);
    } else if (status == 0xB0) {
        auto* host = static_cast<WorkstationHost*>(userData);
        if (host->getRegistry().handleMidiCC(data1, data2)) return;
        if (data1 == 64) engine->setSustainPedal(data2 >= 64);
        else if (data1 == 7) engine->setMasterGain(static_cast<float>(data2) / 100.0f);
        else if (data1 == 10) engine->setPan((static_cast<float>(data2) - 64.0f) / 63.0f);
        else if (data1 == 74) {
            float norm = static_cast<float>(data2) / 127.0f;
            engine->setCutoffHz(20.0f * std::pow(1000.0f, norm));
        } else if (data1 == 71) {
            engine->setResonanceQ(0.5f + (static_cast<float>(data2) / 127.0f) * 11.5f);
        } else if (data1 == 120 || data1 == 123) {
            engine->allNotesOff();
        }
    }
}

// Normaliza el nombre del puerto MIDI en Windows (quita el sufijo numérico que añade WinMM al cambiar de puerto USB)
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
    int portIndex = -1;         // -1 si está desconectado físicamente
    std::string rawName;
    std::string cleanName;
    bool connected = false;     // true si está conectado actualmente al equipo
    bool enabled = false;       // true si el usuario lo seleccionó
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
    explicit HardwareManager(NordPianoEngine& eng) : engine_(eng) {
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

    // Detecta conexiones y desconexiones MIDI en caliente sin bloquear ni perder la lista de teclados guardados
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

        // 1. Marcar como desconectados los puertos que ya no están presentes físicamente
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
                slot.handle.reset(); // Cerrar puerto de forma segura
                slot.connected = false;
                slot.portIndex = -1;
            }
        }

        // 2. Incorporar puertos conectados actuales y reconectar si estaban habilitados por el usuario
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

        // 3. Asegurar que cualquier teclado guardado que hoy esté desconectado figure en la lista como "No conectado"
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

    // Remueve de la lista un dispositivo desconectado que el usuario ya no quiera conservar
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

    NordPianoEngine& engine_;
    std::unique_ptr<RtAudio> dac_;
    std::vector<std::string> availableApis_;
    std::vector<AudioDeviceSpec> devices_;
    int activeUid_ = -1;
    std::string activeApiName_ = "WASAPI";
    std::string activeDeviceName_ = "None";
    unsigned int activeSampleRate_ = 48000;
    unsigned int activeBufferSize_ = 128;

    // Configuración persistente
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

std::string findUiFile(const std::string& filename) {
    for (const auto& prefix : {"ui/", "../ui/", "../../ui/"}) {
        std::string candidate = std::string(prefix) + filename;
        if (fs::exists(candidate)) return candidate;
    }
    return "ui/" + filename;
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
    return {
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
        {"shimmerColorMode", p.shimmerColorMode}
    };
}

SynthPresetParams deserializeParams(const json& j, const SynthPresetParams& fallback) {
    SynthPresetParams p = fallback;
    if (!j.is_object()) return p;
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
    return p;
}

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n";
    std::cout << "   NORD VIRTUAL STAGE 5.0 - STAGE WORKSTATION EDITION    \n";
    std::cout << "=========================================================\n\n";

    

    WorkstationHost engine;
    MidiOscMapper oscMapper(engine);
    oscMapper.startOscServer(9000);


    std::string libraryRootDir = (argc > 1) ? argv[1] : "./samples";
    if (!fs::exists(libraryRootDir)) libraryRootDir = ".";

    std::string statusMsg = "Listo.";
    std::mutex dataMutex;
    int activeProgramSlot = 0;

    // 1. Leer primero programs.json para iniciar directamente con el Programa 1 (Slot 0)
    const std::string programsFilePath = "programs.json";
    json userPrograms = json::array();
    if (fs::exists(programsFilePath)) {
        try {
            std::ifstream pf(programsFilePath);
            userPrograms = json::parse(pf);
        } catch (...) {}
    }

    bool loadedFromProgram1 = false;
    if (userPrograms.is_array() && userPrograms.size() == 8) {
        const auto& prog0 = userPrograms[0];
        std::string targetPath = prog0.value("sourcePath", "");
        std::string targetFile = prog0.value("fileName", "");

        if (!targetPath.empty() && !fs::exists(targetPath) && !targetFile.empty()) {
            fs::path fallbackPath = fs::path(libraryRootDir) / targetFile;
            if (fs::exists(fallbackPath)) targetPath = fallbackPath.generic_string();
        }

        if (!targetPath.empty() && fs::exists(targetPath)) {
            auto initInst = std::make_shared<PianoInstrument>();
            if (BankLoader::loadAny(targetPath, *initInst, statusMsg)) {
                engine.setInstrument(initInst);
                if (prog0.contains("params")) {
                    SynthPresetParams p = deserializeParams(prog0["params"], engine.getCurrentParams());
                    engine.applyPreset(p);
                }
                loadedFromProgram1 = true;
                std::cout << "[Inicio] Programa 1 cargado automaticamente: " << initInst->name << "\n";
            }
        }
    }

    // 2. Si es la primera vez que se abre la app (o no existía el archivo del Programa 1), cargar por defecto de ./samples
    if (!loadedFromProgram1) {
        auto initInst = std::make_shared<PianoInstrument>();
        if (BankLoader::loadAny(libraryRootDir, *initInst, statusMsg)) {
            engine.setInstrument(initInst);
            std::cout << "[Inicio] " << statusMsg << "\n";
        }

        if (!userPrograms.is_array() || userPrograms.size() != 8) {
            userPrograms = json::array();
            auto inst = engine.getInstrument();
            std::string normPath = inst ? fs::path(inst->sourcePath).generic_string() : "";
            for (int i = 0; i < 8; ++i) {
                userPrograms.push_back({
                    {"slot", i + 1},
                    {"name", inst ? inst->name : ("Program " + std::to_string(i + 1))},
                    {"sourcePath", normPath},
                    {"fileName", inst ? fs::path(inst->sourcePath).filename().string() : ""},
                    {"params", serializeParams(engine.getCurrentParams())}
                });
            }
            std::ofstream pf(programsFilePath);
            if (pf) pf << userPrograms.dump(2);
        } else if (userPrograms[0].contains("params")) {
            // Aplicar los parámetros guardados del Programa 1 aunque haya usado el sample de respaldo
            SynthPresetParams p = deserializeParams(userPrograms[0]["params"], engine.getCurrentParams());
            engine.applyPreset(p);
        }
    }

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

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(findUiFile("index.html"));
        if (!f) {
            res.set_content("<h1>Error: No se encontro ui/index.html</h1>", "text/html");
            return;
        }
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        res.set_content(content, "text/html; charset=utf-8");
    });

    svr.Get("/settings", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(findUiFile("settings.html"));
        if (!f) {
            res.set_content("<h1>Error: No se encontro ui/settings.html</h1>", "text/html");
            return;
        }
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        res.set_content(content, "text/html; charset=utf-8");
    });

    svr.Get("/api/state", [&](const httplib::Request&, httplib::Response& res) {
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
            {"instrumentId", inst ? inst->id : 0},
            {"instrumentName", inst ? inst->name : "No Sound Loaded"},
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
        std::string instName = inst ? inst->name : ("Program " + std::to_string(slotIdx + 1));

        userPrograms[slotIdx] = {
            {"slot", slotIdx + 1},
            {"name", instName},
            {"sourcePath", normPath},
            {"fileName", fName},
            {"params", serializeParams(engine.getCurrentParams())}
        };
        std::ofstream pf(programsFilePath);
        if (pf) pf << userPrograms.dump(2);

        res.set_content(json({
            {"ok", true},
            {"slot", slotIdx},
            {"storedName", instName},
            {"programs", userPrograms}
        }).dump(), "application/json");
    });

    svr.Post("/api/programs/recall", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        int slotIdx = std::clamp(body.value("slot", 0), 0, 7);
        activeProgramSlot = slotIdx;
        const auto& prog = userPrograms[slotIdx];

        std::string targetPath = prog.value("sourcePath", "");
        std::string targetFile = prog.value("fileName", "");

        if (!targetPath.empty() && !fs::exists(targetPath) && !targetFile.empty()) {
            fs::path fallbackPath = fs::path(libraryRootDir) / targetFile;
            if (fs::exists(fallbackPath)) targetPath = fallbackPath.generic_string();
        }

        auto curInst = engine.getInstrument();
        std::string curPath = curInst ? fs::path(curInst->sourcePath).generic_string() : "";

        if (!targetPath.empty() && fs::exists(targetPath) && curPath != fs::path(targetPath).generic_string()) {
            auto newInst = std::make_shared<PianoInstrument>();
            if (BankLoader::loadAny(targetPath, *newInst, statusMsg)) {
                engine.setInstrument(newInst);
            }
        }
        if (prog.contains("params")) {
            SynthPresetParams p = deserializeParams(prog["params"], engine.getCurrentParams());
            engine.applyPreset(p);
        } else {
            // Incrementar el contador de revisión de parámetros forzando sincronización en UI
            engine.applyPreset(engine.getCurrentParams());
        }

        auto activeInst = engine.getInstrument();
        res.set_content(json({
            {"ok", true},
            {"slot", slotIdx},
            {"instrumentId", activeInst ? activeInst->id : 0},
            {"instrumentName", activeInst ? activeInst->name : prog.value("name", "Program")},
            {"sourcePath", activeInst ? fs::path(activeInst->sourcePath).generic_string() : ""},
            {"zoneCount", activeInst ? activeInst->zoneCount : 0},
            {"memoryMB", activeInst ? activeInst->memoryMB : 0.0},
            {"params", serializeParams(engine.getCurrentParams())}
        }).dump(), "application/json");
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
            }
        }
        res.set_content(hw.getHardwareStatusJson().dump(), "application/json");
    });

    svr.Post("/api/load", [&](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        std::string targetPath = body.value("path", "");

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
                engine.setInstrument(newInst);
                engine.setMaxVoices(keepParams.maxVoices);
            }
        }

        auto activeInst = engine.getInstrument();
        res.set_content(json({
            {"ok", ok},
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

    svr.Post("/api/param", [&](const httplib::Request& req, httplib::Response& res) {
        auto b = json::parse(req.body, nullptr, false);
        std::string id = b.value("id", "");
        float val = b.value("value", 0.0f);

        if (id == "maxVoices") engine.setMaxVoices(static_cast<int>(val));
        else if (id == "transpose") engine.setTranspose(static_cast<int>(val));
        else if (id == "octave") engine.setOctaveShift(static_cast<int>(val));
        else if (id == "tune") engine.setMasterTuneCents(val);
        else if (id == "cutoff") engine.setCutoffHz(val);
        else if (id == "resonance") engine.setResonanceQ(val);
        else if (id == "attack") engine.setAttackSec(val);
        else if (id == "decay") engine.setDecaySec(val);
        else if (id == "sustain") engine.setSustainLevel(val);
        else if (id == "release") engine.setReleaseSec(val);
        else if (id == "loopHold") engine.setLoopHold(val);
        else if (id == "volume") engine.setMasterGain(val);
        else if (id == "pan") engine.setPan(val);
        else if (id == "softRelease") engine.setSoftRelease(val >= 0.5f);
        else if (id == "touch") engine.setKbdTouch(static_cast<KbdTouch>(static_cast<int>(val)));

        else if (id == "eqEnabled") engine.setEqEnabled(val >= 0.5f);
        else if (id == "lowCut") engine.setLowCutHz(val);
        else if (id == "highCut") engine.setHighCutHz(val);
        else if (id.rfind("eqGain", 0) == 0) engine.setEqBandGainDb(std::stoi(id.substr(6)), val);
        else if (id.rfind("eqFreq", 0) == 0) engine.setEqBandFreqHz(std::stoi(id.substr(6)), val);
        else if (id.rfind("eqQ", 0) == 0)    engine.setEqBandQ(std::stoi(id.substr(3)), val);

        else if (id == "chorusEnabled") engine.setChorusEnabled(val >= 0.5f);
        else if (id == "chorusRate")    engine.setChorusRate(val);
        else if (id == "chorusDepth")   engine.setChorusDepth(val);
        else if (id == "chorusMix")     engine.setChorusMix(val);

        else if (id == "phaserEnabled")  engine.setPhaserEnabled(val >= 0.5f);
        else if (id == "phaserRate")     engine.setPhaserRate(val);
        else if (id == "phaserDepth")    engine.setPhaserDepth(val);
        else if (id == "phaserFeedback") engine.setPhaserFeedback(val);
        else if (id == "phaserMix")      engine.setPhaserMix(val);

        else if (id == "tremoloEnabled") engine.setTremoloEnabled(val >= 0.5f);
        else if (id == "tremoloRate")    engine.setTremoloRate(val);
        else if (id == "tremoloDepth")   engine.setTremoloDepth(val);

        else if (id == "delayEnabled")  engine.setDelayEnabled(val >= 0.5f);
        else if (id == "delayTimeMs")   engine.setDelayTimeMs(val);
        else if (id == "delayFeedback") engine.setDelayFeedback(val);
        else if (id == "delayDamping")  engine.setDelayDamping(val);
        else if (id == "delayMix")      engine.setDelayMix(val);
        else if (id == "delayPingPong") engine.setDelayPingPong(val >= 0.5f);

        else if (id == "reverbEnabled")    engine.setReverbEnabled(val >= 0.5f);
        else if (id == "reverbType")       engine.setReverbType(static_cast<int>(val));
        else if (id == "reverbMix")        engine.setReverbMix(val);
        else if (id == "reverbSize")       engine.setReverbSize(val);
        else if (id == "reverbDamping")    engine.setReverbDamping(val);
        else if (id == "reverbPreDelayMs") engine.setReverbPreDelayMs(val);
        else if (id == "reverbShimmer")    engine.setReverbShimmer(val);

        else if (id == "shimmerShiftSt")    engine.setShimmerShiftSt(val);
        else if (id == "shimmerFeedback")   engine.setShimmerFeedback(val);
        else if (id == "shimmerDiffusion")  engine.setShimmerDiffusion(val);
        else if (id == "shimmerLowCutHz")   engine.setShimmerLowCutHz(val);
        else if (id == "shimmerHighCutHz")  engine.setShimmerHighCutHz(val);
        else if (id == "shimmerModRateHz")  engine.setShimmerModRateHz(val);
        else if (id == "shimmerModDepth")   engine.setShimmerModDepth(val);
        else if (id == "shimmerPitchMode")  engine.setShimmerPitchMode(static_cast<int>(val));
        else if (id == "shimmerReverbMode") engine.setShimmerReverbMode(static_cast<int>(val));
        else if (id == "shimmerColorMode")  engine.setShimmerColorMode(static_cast<int>(val));

        engine.applyParamById(id, val, false);

        res.set_content("{\"ok\":true}", "application/json");
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

    std::cout << "\n[GUI] Nord Virtual Stage: http://localhost:8080\n";
#if defined(_WIN32)
    std::system("start http://localhost:8080");
#elif defined(__APPLE__)
    std::system("open http://localhost:8080");
#endif

    svr.listen("0.0.0.0", 8080);
    return 0;
}