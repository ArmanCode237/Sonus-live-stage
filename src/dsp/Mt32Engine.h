#pragma once
// Roland MT-32 layer engine (synthesis type 6) on Munt's libmt32emu
// (LGPL-2.1+, third_party/mt32emu). libmt32emu is a bit-accurate emulation of the
// LA32 chip, its PCM partials, TVA/TVF/TVP envelopes and the MT-32 reverb, driven
// by the timbre data of the MT-32 control ROM.
//
// ROMs: the MT-32 control ROM and PCM ROM are Roland's copyrighted firmware and are
// NOT part of this project. Place your own dumps (e.g. MT32_CONTROL.ROM and
// MT32_PCM.ROM) in <project>/roms/mt32 or <user data>/roms/mt32; they are
// identified by their SHA-1 like in Munt. Without ROMs the type stays silent.
#include <memory>
#include <string>
#include <vector>

namespace SonusDSP {

struct Mt32RomStatus {
    bool loaded = false;
    std::string control;               // ROM description, e.g. "MT-32 Control v1.07"
    std::string pcm;
    std::string message;               // what was found / missing
    std::vector<std::string> folders;  // where ROMs are searched
    std::vector<std::string> sounds;   // 128 timbre names (group A 0-63, group B 64-127)
};

/** Process-wide ROM images. Load on a control thread before audio starts. */
class Mt32Roms {
public:
    static bool load(const std::vector<std::string>& folders);
    static Mt32RomStatus status();
};

class Mt32LayerEngine {
public:
    /** Opens an emulated MT-32 at the host rate (control thread). Null without ROMs. */
    static std::shared_ptr<Mt32LayerEngine> create(double sampleRate);
    ~Mt32LayerEngine();
    Mt32LayerEngine(const Mt32LayerEngine&) = delete;
    Mt32LayerEngine& operator=(const Mt32LayerEngine&) = delete;

    // Audio thread only. The layer plays Part 1 of its own MT-32.
    void setProgram(int program);              // 0-127 (group A 1-64, group B 1-64)
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void sustain(bool down);
    void setBend(float semitones);             // MT-32 default bender range: 12 semitones
    void setModWheel(float amount);            // 0..1
    void allNotesOff();
    bool active();
    /** Adds numFrames stereo frames into left / right. */
    void render(float* left, float* right, unsigned numFrames);

private:
    Mt32LayerEngine();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SonusDSP
