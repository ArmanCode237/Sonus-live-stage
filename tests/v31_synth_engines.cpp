// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// v31 synth module regression: contour timing, independent filter envelope,
// 24 dB filters, band-limited sync, FM4, LA hybrid, attack transients,
// LFO S&H / delay / key sync, 48-table bank and JSON round trip of the new fields.
#include "../src/modules/StageSynthModule.h"
#include "../src/core/SynthLayerExtras.h"
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
using namespace SonusDSP;

namespace {
constexpr float SR = 48000.f;
constexpr double PI = 3.14159265358979323846;

void solo(StageSynthModule& s, SynthLayerConfig c) {
    for (auto& l : s.layers) { l.cfg = SynthLayerConfig{}; l.cfg.enabled = false; l.reset(); }
    for (auto& f : c.insertFx) f.enabled = false;
    c.enabled = true;
    s.layers[0].cfg = c;
    for (auto& l : s.lfos) l = SynthLFO{};
    s.synthVoiceMaster = true; s.synthVoiceMode = 0; s.synthPortamentoEnabled = false; s.synthMacroCutoff = 1.f;
}
SynthLayerConfig neutral(int wave) {
    SynthLayerConfig c; c.waveType = wave;
    c.cutoffHz = 20000.f; c.resonanceQ = .707f; c.filterEnvAmt = 0; c.filterKeyTrack = 0; c.velToCutoff = 0;
    c.attackSec = .001f; c.decaySec = 1.f; c.sustainLevel = 1.f; c.releaseSec = .1f;
    c.wheelMode = 3; c.noiseLevel = 0; c.unisonVoices = 1;
    return c;
}
std::vector<float> render(StageSynthModule& s, int frames) {
    std::vector<float> st(size_t(frames) * 2, 0.f);
    std::vector<float> mono(static_cast<size_t>(frames));
    for (int off = 0; off < frames; off += 256) s.render(st.data() + size_t(off) * 2, unsigned(std::min(256, frames - off)));
    for (int i = 0; i < frames; ++i) mono[size_t(i)] = st[size_t(i) * 2] + st[size_t(i) * 2 + 1];
    return mono;
}
double rms(const std::vector<float>& x, size_t a, size_t b) {
    double e = 0; for (size_t i = a; i < b; ++i) e += double(x[i]) * x[i]; return std::sqrt(e / double(b - a));
}
double inharmonicDb(const std::vector<float>& x, double f0) {
    const int N = 4096; const size_t start = x.size() - N; double harm = 0, other = 0;
    for (int k = 1; k < N / 2; ++k) {
        std::complex<double> acc = 0;
        for (int n = 0; n < N; ++n)
            acc += double(x[start + size_t(n)]) * (.5 - .5 * std::cos(2 * PI * n / (N - 1))) * std::polar(1.0, -2 * PI * k * n / N);
        const double f = k * SR / N, h = f / f0, d = std::abs(h - std::round(h)) * f0 / (SR / N);
        (std::round(h) >= 1 && d <= 2.5 ? harm : other) += std::norm(acc);
    }
    return 10 * std::log10((other + 1e-30) / (harm + 1e-30));
}
bool finite(const std::vector<float>& x) { for (float v : x) if (!std::isfinite(v) || std::abs(v) > 8.f) return false; return true; }
}

int main() {
    StageSynthModule s; s.init(SR); s.synthEnabled = true; s.synthMasterVol = 1.f; s.voiceLimitPerLayer = 32;

    // Amp attack reaches full level at the set time (±15 %).
    {
        auto c = neutral(3); c.attackSec = .05f; solo(s, c); s.noteOn(60, 60, 127);
        float buf[2]; int n = 0;
        for (; n < int(SR); ++n) { buf[0] = buf[1] = 0; s.render(buf, 1); if (s.layers[0].voices[0].env >= .999f) break; }
        const double ms = n * 1000.0 / SR;
        std::printf("attack 50 ms -> full level at %.1f ms\n", ms);
        assert(ms > 42.5 && ms < 57.5);
        s.allNotesOff();
    }
    // Filter envelope is independent of the amp attack.
    {
        auto c = neutral(0); c.attackSec = .005f; c.filtAttackSec = .3f; c.filtDecaySec = 1.f; c.filtSustainLvl = 0.f; c.filterEnvAmt = 1.f;
        solo(s, c); s.noteOn(60, 60, 127); float peak = 0, buf[2];
        for (int n = 0; n < int(SR * .6f); ++n) { buf[0] = buf[1] = 0; s.render(buf, 1); peak = std::max(peak, s.layers[0].voices[0].filtEnv); }
        std::printf("filter env peak %.3f\n", peak);
        assert(peak > .99f);
        s.allNotesOff();
    }
    // 4-pole low-pass: at least 26 dB down one octave above cutoff (12 dB filter: ~13 dB).
    for (int type : {4, 5}) {
        auto open = neutral(3); solo(s, open); s.noteOn(96, 96, 127); const double ref = rms(render(s, 24000), 12000, 24000); s.allNotesOff();
        auto c = neutral(3); c.filterType = type; c.cutoffHz = 1000.f; solo(s, c); s.noteOn(96, 96, 127);
        const double db = 20 * std::log10(rms(render(s, 24000), 12000, 24000) / ref); s.allNotesOff();
        std::printf("filter %d at 2 kHz: %.1f dB\n", type, db);
        assert(db < -26.0);
    }
    // Band-limited hard sync stays below -20 dB of inharmonic (aliased) energy at C7.
    for (int wave : {7, 27, 32}) {
        auto c = neutral(wave); c.osc2Level = .8f; c.osc2Detune = 0; solo(s, c); s.noteOn(96, 96, 127);
        const double db = inharmonicDb(render(s, 12288), 440 * std::pow(2, 27 / 12.0)); s.allNotesOff();
        std::printf("sync wave %d aliasing %.1f dB\n", wave, db);
        assert(db < -20.0);
    }
    // Every new wave, all FM4 algorithms, LA and every transient: finite, bounded, audible.
    auto audible = [&](SynthLayerConfig c, int key, const char* what) {
        solo(s, c); s.noteOn(key, key, 110); auto x = render(s, 24000); s.allNotesOff();
        double peak = 0; for (float v : x) peak = std::max(peak, double(std::abs(v)));
        if (!finite(x) || peak < 1e-3) { std::printf("FAIL %s peak=%g\n", what, peak); assert(false); }
    };
    for (int w = 29; w < SYNTH_WAVE_COUNT; ++w) { auto c = neutral(w); c.osc2Level = .7f; audible(c, 60, "wave"); audible(c, 108, "wave high"); }
    for (int a = 0; a < 8; ++a) { auto c = neutral(0); c.synthesisType = 3; c.fm4Algorithm = a; c.fm4Feedback = 1.f; audible(c, 48, "fm4"); audible(c, 108, "fm4 high"); }
    { auto c = neutral(0); c.synthesisType = 4; c.wavetableIdx = 40; c.laRing = .5f; audible(c, 60, "la"); }
    for (int t = 0; t < SYNTH_TRANSIENT_COUNT; ++t) {
        auto c = neutral(3); c.sustainLevel = 0; c.decaySec = .01f; c.transientType = t; c.transientLevel = 1.f; audible(c, 72, "transient");
    }
    for (int type : {4, 5}) { auto c = neutral(0); c.filterType = type; c.resonanceQ = 12.f; c.cutoffHz = 300.f; c.drive = 1.f; audible(c, 36, "max resonance"); }

    // LFO: sample & hold produces steps; delay keeps modulation silent at first.
    {
        auto c = neutral(3); solo(s, c);
        auto& l = s.lfos[0]; l.enabled = true; l.waveType = 4; l.rateHz = 20.f; l.depth = 1.f; l.destPitch = 100;
        s.noteOn(60, 60, 100); render(s, 9600);
        std::vector<float> seen; float buf[2];
        for (int n = 0; n < 4800; ++n) { buf[0] = buf[1] = 0; s.render(buf, 1); if (seen.empty() || std::abs(seen.back() - l.currentVal) > 1e-6f) seen.push_back(l.currentVal); }
        std::printf("S&H distinct values in 100 ms at 20 Hz: %zu\n", seen.size());
        assert(seen.size() >= 2 && seen.size() <= 4);
        s.allNotesOff();
        solo(s, c);
        auto& d = s.lfos[0]; d.enabled = true; d.waveType = 0; d.rateHz = 5.f; d.depth = 1.f; d.delaySec = .5f; d.fadeSec = .2f; d.keySync = true; d.destPitch = 100;
        s.noteOn(60, 60, 100); render(s, 9600);
        assert(std::abs(d.currentVal) < 1e-6f);          // still inside the 0.5 s delay
        render(s, 24000);                                  // past delay + fade
        float swing = 0.f;
        for (int n = 0; n < 9600; ++n) { buf[0] = buf[1] = 0; s.render(buf, 1); swing = std::max(swing, std::abs(d.currentVal)); }
        std::printf("LFO after delay+fade: peak %.2f\n", swing);
        assert(swing > .9f);
        s.allNotesOff();
    }
    // Wavetable bank: 36 legacy + 12 v31 tables.
    assert(s.getWavetableSnapshot()->size() == 48);
    // JSON round trip of every v31 layer field.
    {
        SynthLayerConfig a; a.analog = .4f; a.fm4Algorithm = 5; a.fm4Feedback = .3f; a.fm4VelMod = .7f;
        a.opRatio = {1.f, 3.5f, 14.f, .5f}; a.opLevel = {.9f, .4f, .2f, .1f}; a.opAttack = {.01f, .02f, .03f, .04f};
        a.opDecay = {1.f, 2.f, 3.f, 4.f}; a.opSustain = {.1f, .2f, .3f, .4f};
        a.laMix = .25f; a.laRing = .6f; a.transientType = 6; a.transientLevel = .8f; a.transientDecay = .3f; a.transientTone = .2f;
        nlohmann::json j; synthLayerExtrasToJson(a, j);
        SynthLayerConfig b; synthLayerExtrasFromJson(j, b);
        assert(b.analog == a.analog && b.fm4Algorithm == a.fm4Algorithm && b.opRatio == a.opRatio && b.opSustain == a.opSustain);
        assert(b.laRing == a.laRing && b.transientType == a.transientType && b.transientTone == a.transientTone);
        SynthLayerConfig legacy; synthLayerExtrasFromJson(nlohmann::json::object(), legacy);
        assert(legacy.transientLevel == 0.f && legacy.analog == 0.f);   // old patches: no transient, no drift
    }
    std::puts("PASS v31 synth engines");
}
