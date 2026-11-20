#pragma once
// DX7 voice engine for synth layers (synthesis type 5), built on msfa - the
// Apache-2.0 FM core by Raph Levien used by Dexed (third_party/msfa, NOTICE.txt).
// msfa reproduces the DX7 itself: 6 operators, the 32 algorithms with feedback,
// the exponential rate/level envelopes, keyboard level and rate scaling, velocity
// tables, the pitch EG and the DX7 LFO. Patches are DX7 "VCED" voices (155 bytes,
// plus 1 operator-enable byte) loaded from standard 32-voice .syx cartridges.
//
// msfa headers are kept out of this header (pimpl): they define global helpers
// (N, min, max) that must not leak into the rest of the engine.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace SonusDSP {

constexpr int DX7_VOICE_BYTES = 156;           // 155 VCED bytes + operator on/off mask
using Dx7Voice = std::array<uint8_t, DX7_VOICE_BYTES>;

/** The DX7 "INIT VOICE": algorithm 1, OP1 sine at full level. */
Dx7Voice dx7InitVoice();
/** Voice name (10 characters, trailing spaces trimmed). */
std::string dx7VoiceName(const Dx7Voice& voice);

/** Voice <-> 312-character hex string (presets / programs JSON). */
std::string dx7ToHex(const Dx7Voice& voice);
bool dx7FromHex(const std::string& hex, Dx7Voice& voice);
/** Unique revision stamp for a newly assigned voice (the audio thread reloads on change). */
uint32_t nextDx7Revision();

/** A 32-voice cartridge parsed from a sysex dump. */
struct Dx7Cartridge {
    std::string name;                  // file name without extension
    std::vector<Dx7Voice> voices;      // 32 voices
};
/** Parses a DX7 32-voice bulk dump (F0 43 0n 09 20 00 + 4096 bytes + checksum + F7)
    or a bare 4096-byte packed bank. Also accepts a single-voice dump (155 bytes).
    Returns false (and an error) for anything else. */
bool parseDx7Sysex(const std::vector<uint8_t>& bytes, std::vector<Dx7Voice>& out, std::string& error);

class Dx7LayerEngine {
public:
    static constexpr int kVoices = 32;
    Dx7LayerEngine();
    ~Dx7LayerEngine();
    Dx7LayerEngine(const Dx7LayerEngine&) = delete;
    Dx7LayerEngine& operator=(const Dx7LayerEngine&) = delete;

    /** Builds msfa's shared tables for a sample rate (control thread, before audio). */
    static void initTables(double sampleRate);

    // Audio thread only ------------------------------------------------------
    /** New notes use this voice; the LFO restarts with its settings. */
    void setVoice(const Dx7Voice& voice);
    void noteOn(int slot, int midiNote, int velocity);
    void noteOff(int slot);
    void kill(int slot);
    bool playing(int slot) const;
    /** Pitch bend in semitones, mod wheel 0..1, fine tune in cents. */
    void setControllers(float bendSemis, float modWheel, float fineCents);
    /** Adds numFrames mono samples (DX7 output) for every active slot. */
    void render(float* out, unsigned numFrames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SonusDSP
