// MT-32 layer engine on libmt32emu (Munt, LGPL-2.1+). See Mt32Engine.h.
#include "Mt32Engine.h"

#include <mt32emu.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>

namespace SonusDSP {
namespace fs = std::filesystem;

namespace {
std::mutex romMutex;
const MT32Emu::ROMImage* controlRom = nullptr;
const MT32Emu::ROMImage* pcmRom = nullptr;
Mt32RomStatus romStatus;

std::string describe(const MT32Emu::ROMInfo* info) {
    if (!info) return {};
    return std::string(info->description ? info->description : info->shortName ? info->shortName : "ROM");
}
} // namespace

bool Mt32Roms::load(const std::vector<std::string>& folders) {
    std::lock_guard<std::mutex> lock(romMutex);
    romStatus.folders = folders;
    if (controlRom && pcmRom) return true;                 // loaded once per process
    // Only complete (non-split) MT-32 / CM-32L ROM images are accepted.
    const MT32Emu::ROMImage* control = nullptr;
    const MT32Emu::ROMImage* pcm = nullptr;
    std::vector<std::string> seen;
    for (const auto& folder : folders) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(folder, ec)) {
            if (!entry.is_regular_file()) continue;
            const auto size = entry.file_size(ec);
            if (ec || size < 32768 || size > 4 * 1024 * 1024) continue;
            auto* file = new MT32Emu::FileStream();
            if (!file->open(entry.path().string().c_str())) { delete file; continue; }
            const MT32Emu::ROMImage* image = MT32Emu::ROMImage::makeROMImage(file);
            const MT32Emu::ROMInfo* info = image ? image->getROMInfo() : nullptr;
            if (!info || info->pairType != MT32Emu::ROMInfo::Full) {
                if (image) MT32Emu::ROMImage::freeROMImage(image);
                delete file;
                continue;
            }
            seen.push_back(entry.path().filename().string() + " = " + describe(info));
            if (info->type == MT32Emu::ROMInfo::Control && !control) control = image;
            else if (info->type == MT32Emu::ROMInfo::PCM && !pcm) pcm = image;
            else { MT32Emu::ROMImage::freeROMImage(image); delete file; }   // duplicates
        }
    }
    if (!control || !pcm) {
        romStatus.loaded = false;
        romStatus.message = std::string(control ? "Falta la PCM ROM. " : "Falta la Control ROM. ") +
            (pcm || control ? "" : "No se encontraron ROMs del MT-32. ") +
            "Copia tus volcados (p. ej. MT32_CONTROL.ROM y MT32_PCM.ROM) en una de las carpetas y reinicia la app.";
        if (control) MT32Emu::ROMImage::freeROMImage(control);
        if (pcm) MT32Emu::ROMImage::freeROMImage(pcm);
        return false;
    }
    controlRom = control; pcmRom = pcm;
    romStatus.loaded = true;
    romStatus.control = describe(control->getROMInfo());
    romStatus.pcm = describe(pcm->getROMInfo());
    romStatus.message = "ROMs cargadas: " + romStatus.control + " + " + romStatus.pcm;
    // Timbre names straight from the control ROM (group A and B, 64 each).
    MT32Emu::Synth probe;
    romStatus.sounds.clear();
    if (probe.open(*controlRom, *pcmRom)) {
        char name[32];
        for (int group = 0; group < 2; ++group)
            for (int n = 0; n < 64; ++n)
                romStatus.sounds.push_back(probe.getSoundName(name, MT32Emu::Bit8u(group), MT32Emu::Bit8u(n)) ? std::string(name) : "#" + std::to_string(group * 64 + n + 1));
        probe.close();
    }
    return true;
}

Mt32RomStatus Mt32Roms::status() {
    std::lock_guard<std::mutex> lock(romMutex);
    return romStatus;
}

// ---------------------------------------------------------------- layer engine
struct Mt32LayerEngine::Impl {
    MT32Emu::Synth synth;
    std::unique_ptr<MT32Emu::SampleRateConverter> converter;
    std::vector<float> interleaved;
    int program = -1;
    int bend = 0x2000;
    int wheel = -1;
    // Part 1 listens on MIDI channel 2 (MT-32 default): status bytes carry channel 1.
    void send(uint8_t status, uint8_t d1, uint8_t d2) {
        synth.playMsgOnPart(0, uint8_t(status >> 4), d1, d2);
    }
};

Mt32LayerEngine::Mt32LayerEngine() : impl_(std::make_unique<Impl>()) {}
Mt32LayerEngine::~Mt32LayerEngine() {
    if (impl_) { impl_->converter.reset(); impl_->synth.close(); }
}

std::shared_ptr<Mt32LayerEngine> Mt32LayerEngine::create(double sampleRate) {
    std::lock_guard<std::mutex> lock(romMutex);
    if (!controlRom || !pcmRom) return nullptr;
    std::shared_ptr<Mt32LayerEngine> engine(new Mt32LayerEngine());
    auto& d = *engine->impl_;
    if (!d.synth.open(*controlRom, *pcmRom, 32u /* MT-32 partial count */, MT32Emu::AnalogOutputMode_ACCURATE)) return nullptr;
    d.synth.setReverbEnabled(true);       // the MT-32's own reverb is part of its sound
    d.converter = std::make_unique<MT32Emu::SampleRateConverter>(d.synth, sampleRate,
        MT32Emu::SamplerateConversionQuality_GOOD);
    d.interleaved.assign(2 * 8192, 0.f);
    return engine;
}

void Mt32LayerEngine::setProgram(int program) {
    program = std::clamp(program, 0, 127);
    if (program == impl_->program) return;
    impl_->program = program;
    impl_->send(0xC0, uint8_t(program), 0);
}
void Mt32LayerEngine::noteOn(int note, int velocity) {
    impl_->send(0x90, uint8_t(std::clamp(note, 0, 127)), uint8_t(std::clamp(velocity, 1, 127)));
}
void Mt32LayerEngine::noteOff(int note) { impl_->send(0x80, uint8_t(std::clamp(note, 0, 127)), 0); }
void Mt32LayerEngine::sustain(bool down) { impl_->send(0xB0, 64, down ? 127 : 0); }
void Mt32LayerEngine::setBend(float semitones) {
    const int value = std::clamp(0x2000 + int(std::lround(semitones / 12.f * 8192.f)), 0, 0x3fff);
    if (value == impl_->bend) return;
    impl_->bend = value;
    impl_->send(0xE0, uint8_t(value & 0x7f), uint8_t(value >> 7));
}
void Mt32LayerEngine::setModWheel(float amount) {
    const int value = std::clamp(int(std::lround(amount * 127.f)), 0, 127);
    if (value == impl_->wheel) return;
    impl_->wheel = value;
    impl_->send(0xB0, 1, uint8_t(value));
}
void Mt32LayerEngine::allNotesOff() { impl_->send(0xB0, 123, 0); }
bool Mt32LayerEngine::active() { return impl_->synth.isActive(); }

void Mt32LayerEngine::render(float* left, float* right, unsigned numFrames) {
    auto& d = *impl_;
    unsigned done = 0;
    while (done < numFrames) {
        const unsigned take = std::min<unsigned>(numFrames - done, unsigned(d.interleaved.size() / 2));
        d.converter->getOutputSamples(d.interleaved.data(), take);
        for (unsigned i = 0; i < take; ++i) {
            left[done + i] += d.interleaved[2 * i];
            right[done + i] += d.interleaved[2 * i + 1];
        }
        done += take;
    }
}

} // namespace SonusDSP
