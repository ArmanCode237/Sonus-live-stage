// DX7 layer engine on top of msfa (Apache-2.0, third_party/msfa/NOTICE.txt).
// The glue below (sysex unpacking, voice slots, block FIFO) is Live Stage code,
// written from the public Yamaha DX7 sysex specification.
#include "Dx7Engine.h"

#include "../../third_party/msfa/synth.h"
#include "../../third_party/msfa/controllers.h"
#include "../../third_party/msfa/dx7note.h"
#include "../../third_party/msfa/env.h"
#include "../../third_party/msfa/exp2.h"
#include "../../third_party/msfa/fm_core.h"
#include "../../third_party/msfa/freqlut.h"
#include "../../third_party/msfa/lfo.h"
#include "../../third_party/msfa/pitchenv.h"
#include "../../third_party/msfa/porta.h"
#include "../../third_party/msfa/sin.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace SonusDSP {

namespace {
constexpr int kVcedBytes = 155;
constexpr int kPackedVoice = 128;

// One packed voice (the 32-voice bulk format) -> VCED (155 bytes) + op mask.
// Operators are stored OP6 first in both formats.
Dx7Voice unpackVoice(const uint8_t* p) {
    Dx7Voice v{};
    for (int op = 0; op < 6; ++op) {
        const uint8_t* s = p + op * 17;
        uint8_t* d = v.data() + op * 21;
        for (int i = 0; i < 11; ++i) d[i] = s[i] & 0x7f;          // R1-4, L1-4, BP, LD, RD
        d[11] = s[11] & 0x03;                                     // left curve
        d[12] = (s[11] >> 2) & 0x03;                              // right curve
        d[13] = s[12] & 0x07;                                     // rate scaling
        d[14] = s[13] & 0x03;                                     // amp mod sensitivity
        d[15] = (s[13] >> 2) & 0x07;                              // key velocity sensitivity
        d[16] = s[14] & 0x7f;                                     // output level
        d[17] = s[15] & 0x01;                                     // oscillator mode
        d[18] = (s[15] >> 1) & 0x1f;                              // frequency coarse
        d[19] = s[16] & 0x7f;                                     // frequency fine
        d[20] = (s[12] >> 3) & 0x0f;                              // detune
    }
    for (int i = 0; i < 8; ++i) v[126 + i] = p[102 + i] & 0x7f;  // pitch EG R1-4, L1-4
    v[134] = p[110] & 0x1f;                                       // algorithm
    v[135] = p[111] & 0x07;                                       // feedback
    v[136] = (p[111] >> 3) & 0x01;                                // oscillator key sync
    v[137] = p[112] & 0x7f;                                       // LFO speed
    v[138] = p[113] & 0x7f;                                       // LFO delay
    v[139] = p[114] & 0x7f;                                       // LFO pitch mod depth
    v[140] = p[115] & 0x7f;                                       // LFO amp mod depth
    v[141] = p[116] & 0x01;                                       // LFO key sync
    v[142] = (p[116] >> 1) & 0x07;                                // LFO waveform
    v[143] = (p[116] >> 4) & 0x07;                                // pitch mod sensitivity
    v[144] = p[117] & 0x7f;                                       // transpose (24 = C3)
    for (int i = 0; i < 10; ++i) v[145 + i] = p[118 + i] & 0x7f;  // name
    v[155] = 0x3f;                                                // all operators on
    return v;
}

// Keeps a voice inside the documented DX7 parameter ranges (corrupt banks are common).
void clampVoice(Dx7Voice& v) {
    static const uint8_t opMax[21] = {99,99,99,99,99,99,99,99,99,99,99,3,3,7,3,7,99,1,31,99,14};
    for (int op = 0; op < 6; ++op)
        for (int i = 0; i < 21; ++i) v[op * 21 + i] = std::min(v[op * 21 + i], opMax[i]);
    static const uint8_t globalMax[19] = {99,99,99,99,99,99,99,99,31,7,1,99,99,99,99,1,5,7,48};
    for (int i = 0; i < 19; ++i) v[126 + i] = std::min(v[126 + i], globalMax[i]);
    for (int i = 145; i < 155; ++i) if (v[i] < 32 || v[i] > 126) v[i] = ' ';
}
} // namespace

Dx7Voice dx7InitVoice() {
    Dx7Voice v{};
    for (int op = 0; op < 6; ++op) {
        static const uint8_t opInit[21] = {99,99,99,99,99,99,99,0,39,0,0,0,0,0,0,0,0,0,1,0,7};
        std::memcpy(v.data() + op * 21, opInit, 21);
    }
    v[5 * 21 + 16] = 99;                                          // OP1 (last stored) at full level
    const uint8_t global[19] = {99,99,99,99,50,50,50,50,0,0,1,35,0,0,0,1,0,3,24};
    std::memcpy(v.data() + 126, global, 19);
    std::memcpy(v.data() + 145, "INIT VOICE", 10);
    v[155] = 0x3f;
    return v;
}

std::string dx7VoiceName(const Dx7Voice& voice) {
    std::string name(reinterpret_cast<const char*>(voice.data() + 145), 10);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    return name;
}

std::string dx7ToHex(const Dx7Voice& voice) {
    static const char* digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(voice.size() * 2);
    for (uint8_t b : voice) { hex += digits[b >> 4]; hex += digits[b & 15]; }
    return hex;
}

bool dx7FromHex(const std::string& hex, Dx7Voice& voice) {
    if (hex.size() != voice.size() * 2) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    Dx7Voice v{};
    for (size_t i = 0; i < v.size(); ++i) {
        const int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        v[i] = uint8_t(hi * 16 + lo);
    }
    clampVoice(v);
    if (v[155] == 0) v[155] = 0x3f;
    voice = v;
    return true;
}

uint32_t nextDx7Revision() {
    static std::atomic<uint32_t> revision{0};
    return revision.fetch_add(1, std::memory_order_relaxed) + 1;
}

bool parseDx7Sysex(const std::vector<uint8_t>& b, std::vector<Dx7Voice>& out, std::string& error) {
    out.clear();
    const uint8_t* packed = nullptr;
    if (b.size() >= 4104 && b[0] == 0xF0 && b[1] == 0x43 && b[3] == 0x09) packed = b.data() + 6;
    else if (b.size() == 4096) packed = b.data();
    if (packed) {
        for (int i = 0; i < 32; ++i) { out.push_back(unpackVoice(packed + i * kPackedVoice)); clampVoice(out.back()); }
        return true;
    }
    if (b.size() >= 163 && b[0] == 0xF0 && b[1] == 0x43 && b[3] == 0x00) {   // single voice (VCED)
        Dx7Voice v{};
        std::memcpy(v.data(), b.data() + 6, kVcedBytes);
        v[155] = 0x3f;
        clampVoice(v);
        out.push_back(v);
        return true;
    }
    error = "No es un volcado DX7 (se espera un cartucho de 32 voces de 4104 bytes o una voz de 163 bytes)";
    return false;
}

// ---------------------------------------------------------------- engine
struct Dx7LayerEngine::Impl {
    FmCore core;
    Controllers ctrls;
    Lfo lfo;
    Dx7Voice voice = dx7InitVoice();
    std::array<Dx7Note, kVoices> notes;
    std::array<bool, kVoices> active{};
    std::array<bool, kVoices> down{};
    std::array<int, kVoices> silentBlocks{};
    int silentLimit = 172;         // blocks of 64 samples (~0.25 s at 44.1 kHz)
    float fifo[N] = {};
    int fifoPos = N;               // N = empty

    Impl() {
        ctrls.core = &core;
        ctrls.pitch_cc = 0x2000;
        ctrls.pitch_range_up = ctrls.pitch_range_dn = 12;   // bend arrives in semitones
        ctrls.pitch_step = 0;
        ctrls.masterTune = 0;
        ctrls.modwheel_cc = ctrls.breath_cc = ctrls.foot_cc = ctrls.aftertouch_cc = 0;
        ctrls.portamento_enable_cc = false;
        ctrls.portamento_cc = 0;
        ctrls.portamento_gliss_cc = false;
        ctrls.mpeEnabled = false;
        // DX7 function defaults: the mod wheel adds LFO pitch and amplitude modulation.
        ctrls.wheel.range = 99; ctrls.wheel.pitch = true; ctrls.wheel.amp = true; ctrls.wheel.eg = false;
        ctrls.refresh();
        lfo.reset(voice.data() + 137);
    }

    void computeBlock() {
        alignas(16) int32_t buf[N];
        const int32_t lfoValue = lfo.getsample();
        const int32_t lfoDelay = lfo.getdelay();
        std::fill(fifo, fifo + N, 0.f);
        for (int slot = 0; slot < kVoices; ++slot) {
            if (!active[slot]) continue;
            std::fill(buf, buf + N, 0);
            notes[slot].compute(buf, lfoValue, lfoDelay, &ctrls);
            int peak = 0;
            for (int j = 0; j < N; ++j) {
                peak = std::max(peak, std::abs(buf[j]));
                // Dexed's output stage: 24-bit with the DX7 DAC headroom, hard limited.
                const int32_t val = buf[j] >> 4;
                const int clip = val < -(1 << 24) ? -0x8000 : val >= (1 << 24) ? 0x7fff : (val >> 9);
                fifo[j] += float(clip) * (1.f / 32768.f);
            }
            // A released note ends when every carrier envelope has finished, or when
            // it stays below -96 dB (DX7 voices with L4 > 0 never finish by themselves).
            if (!down[slot] && !notes[slot].isPlaying()) active[slot] = false;
            silentBlocks[slot] = (!down[slot] && peak < (1 << 12)) ? silentBlocks[slot] + 1 : 0;
            if (silentBlocks[slot] > silentLimit) active[slot] = false;
        }
        fifoPos = 0;
    }
};

Dx7LayerEngine::Dx7LayerEngine() : impl_(std::make_unique<Impl>()) {}
Dx7LayerEngine::~Dx7LayerEngine() = default;

void Dx7LayerEngine::initTables(double sampleRate) {
    static bool constantTables = false;
    if (!constantTables) { Exp2::init(); Tanh::init(); Sin::init(); constantTables = true; }
    Freqlut::init(sampleRate);
    Lfo::init(sampleRate);
    PitchEnv::init(sampleRate);
    Env::init_sr(sampleRate);
    Porta::init_sr(sampleRate);
}

void Dx7LayerEngine::setVoice(const Dx7Voice& voice) {
    impl_->voice = voice;
    impl_->lfo.reset(impl_->voice.data() + 137);
}

void Dx7LayerEngine::noteOn(int slot, int midiNote, int velocity) {
    if (slot < 0 || slot >= kVoices) return;
    auto& d = *impl_;
    // The patch transpose (byte 144, 24 = C3) is applied by msfa itself.
    d.notes[slot].init(d.voice.data(), std::clamp(midiNote, 0, 127), std::clamp(velocity, 1, 127), 1, &d.ctrls);
    d.lfo.keydown();
    d.active[slot] = true;
    d.down[slot] = true;
    d.silentBlocks[slot] = 0;
}

void Dx7LayerEngine::noteOff(int slot) {
    if (slot < 0 || slot >= kVoices) return;
    auto& d = *impl_;
    if (d.active[slot] && d.down[slot]) { d.notes[slot].keyup(); d.down[slot] = false; }
}

void Dx7LayerEngine::kill(int slot) {
    if (slot < 0 || slot >= kVoices) return;
    impl_->active[slot] = false;
    impl_->down[slot] = false;
}

bool Dx7LayerEngine::playing(int slot) const {
    return slot >= 0 && slot < kVoices && impl_->active[slot];
}

void Dx7LayerEngine::setControllers(float bendSemis, float modWheel, float fineCents) {
    auto& c = impl_->ctrls;
    // pitch range 12: one semitone = 8192 / 12 steps of the 14-bit bend.
    c.pitch_cc = std::clamp(0x2000 + int(std::lround(bendSemis * (8192.0 / 12.0))), 0, 0x3fff);
    c.masterTune = int(std::lround(fineCents * double(1 << 24) / 1200.0));
    const int wheel = std::clamp(int(std::lround(modWheel * 127.f)), 0, 127);
    if (wheel != c.modwheel_cc) { c.modwheel_cc = wheel; c.refresh(); }
}

void Dx7LayerEngine::render(float* out, unsigned numFrames) {
    auto& d = *impl_;
    unsigned done = 0;
    while (done < numFrames) {
        if (d.fifoPos >= N) d.computeBlock();
        const unsigned take = std::min<unsigned>(numFrames - done, unsigned(N - d.fifoPos));
        for (unsigned i = 0; i < take; ++i) out[done + i] += d.fifo[d.fifoPos + i];
        d.fifoPos += int(take);
        done += take;
    }
}

} // namespace SonusDSP
