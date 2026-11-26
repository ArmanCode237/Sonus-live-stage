// Assertions carry the checks: keep them in Release builds.
#undef NDEBUG
// v32.3 synth modulation matrix: global sources move a copy of the layer config (the
// knob base is untouched), per-voice sources only reach voice targets, ranges follow
// "from = knob, to = knob + amount".
#include "modules/StageSynthModule.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace SonusDSP;

static double renderEnergy(StageSynthModule& s, int blocks, double* hf = nullptr) {
    std::vector<float> b(512);
    double e = 0, d = 0; float prev = 0;
    for (int k = 0; k < blocks; ++k) {
        std::fill(b.begin(), b.end(), 0.f);
        s.render(b.data(), 256);
        for (size_t i = 0; i < b.size(); i += 2) { assert(std::isfinite(b[i])); e += b[i] * b[i]; d += (b[i] - prev) * (b[i] - prev); prev = b[i]; }
    }
    if (hf) *hf = d / std::max(e, 1e-12);
    return e;
}

static void setupSaw(StageSynthModule& s) {
    s.init(44100.f); s.synthEnabled = true; s.synthMasterVol = .8f;
    for (auto& l : s.layers) l.cfg.enabled = false;
    auto& c = s.layers[0].cfg;
    c.enabled = true; c.synthesisType = 0; c.waveType = 0; c.cutoffHz = 400.f; c.filterEnvAmt = 0.f; c.velToCutoff = 0.f;
    for (auto& fx : c.insertFx) fx.enabled = false;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    {   // wheel -> resonance (block target): from the knob (2.0) to knob + 0.5 of its range
        StageSynthModule s; setupSaw(s);
        s.layers[0].cfg.resonanceQ = 2.f;
        s.setModMatrix({{MOD_WHEEL, "L1_res", .5f, false, 0}});
        assert(s.getModMatrix().size() == 1);
        s.layers[0].performanceModWheel = 0.f;
        renderEnergy(s, 1);
        assert(std::abs(s.layers[0].runCfg.resonanceQ - 2.f) < 1e-3f);
        s.layers[0].performanceModWheel = 1.f;
        renderEnergy(s, 1);
        const float expected = 2.f + .5f * (12.f - .5f);
        std::printf("wheel 0 -> Q 2.00, wheel 1 -> Q %.2f (expected %.2f), knob still %.2f\n",
                    s.layers[0].runCfg.resonanceQ, expected, s.layers[0].cfg.resonanceQ);
        assert(std::abs(s.layers[0].runCfg.resonanceQ - expected) < 1e-3f && s.layers[0].cfg.resonanceQ == 2.f);
    }
    {   // wheel -> cutoff (voice target, global source): brighter with the wheel up
        StageSynthModule s; setupSaw(s);
        s.setModMatrix({{MOD_WHEEL, "L1_cut", .5f, false, 0}});
        double hfDown = 0, hfUp = 0;
        s.layers[0].performanceModWheel = 0.f; s.noteOn(48, 48, 100); renderEnergy(s, 40, &hfDown);
        s.layers[0].performanceModWheel = 1.f; renderEnergy(s, 40, &hfUp);
        std::printf("wheel -> cutoff: wheel 0 hf %.4f, wheel 1 hf %.4f\n", hfDown, hfUp);
        assert(hfUp > hfDown * 2 && "the wheel must open the filter");
    }
    {   // per-voice sources are rejected on non-voice targets, accepted on voice targets
        StageSynthModule s; setupSaw(s);
        s.setModMatrix({{MOD_VELOCITY, "L1_res", .5f, false, 0}, {MOD_VELOCITY, "L1_cut", .6f, false, 0}, {MOD_LFO1, "L9_cut", .2f, false, 0}});
        assert(s.getModMatrix().size() == 1 && s.getModMatrix()[0].target == "L1_cut");
        double hfSoft = 0, hfHard = 0;
        s.noteOn(48, 48, 10); renderEnergy(s, 40, &hfSoft); s.allNotesOff(); renderEnergy(s, 40);
        s.noteOn(48, 48, 127); renderEnergy(s, 40, &hfHard);
        std::printf("velocity -> cutoff per voice: soft hf %.4f, hard hf %.4f\n", hfSoft, hfHard);
        assert(hfHard > hfSoft * 1.5 && "velocity must open the filter per voice");
    }
    {   // macro -> insert mix (global source on an insert target)
        StageSynthModule s; setupSaw(s);
        s.layers[0].cfg.insertFx[0].enabled = true; s.layers[0].cfg.insertFx[0].mix = .1f;
        s.setModMatrix({{MOD_MACRO1, "L1_fx0_mix", .6f, false, 0}});
        s.macros[0] = 1.f; renderEnergy(s, 1);
        std::printf("macro 1 -> chorus mix: %.2f (knob %.2f)\n", s.layers[0].runCfg.insertFx[0].mix, s.layers[0].cfg.insertFx[0].mix);
        assert(std::abs(s.layers[0].runCfg.insertFx[0].mix - .7f) < 1e-3f && s.layers[0].cfg.insertFx[0].mix == .1f);
    }
    {   // v32.4 per-layer output GAIN after the inserts: +12 dB = x3.98 amplitude (x15.8 energy), glided
        StageSynthModule a; setupSaw(a); StageSynthModule b; setupSaw(b);
        b.layers[0].cfg.outGainDb = 12.f;
        a.noteOn(48, 48, 100); b.noteOn(48, 48, 100);
        renderEnergy(a, 20); renderEnergy(b, 20);                       // let the 10 ms gain glide settle
        const double ea = renderEnergy(a, 40), eb = renderEnergy(b, 40);
        std::printf("layer GAIN +12 dB: energy ratio %.2f (expected 15.85)\n", eb / ea);
        assert(std::abs(eb / ea - 15.85) < .5);
    }
    {   // v32.4 synth module output GAIN: +6 dB on the whole module = x3.98 energy
        StageSynthModule a; setupSaw(a); StageSynthModule b; setupSaw(b);
        b.synthOutGainDb = 6.f;
        a.noteOn(48, 48, 100); b.noteOn(48, 48, 100);
        renderEnergy(a, 20); renderEnergy(b, 20);
        const double ea = renderEnergy(a, 40), eb = renderEnergy(b, 40);
        std::printf("synth GAIN +6 dB: energy ratio %.2f (expected 3.98)\n", eb / ea);
        assert(std::abs(eb / ea - 3.98) < .2);
    }
    {   // v32.4 layer EQ: HP 1 kHz / 48 dB removes most of a low saw; a +12 dB band adds energy
        auto run = [](void (*setup)(SynthLayerConfig&)) {
            StageSynthModule s; setupSaw(s);
            s.layers[0].cfg.cutoffHz = 12000.f;
            setup(s.layers[0].cfg);
            s.noteOn(36, 36, 100); renderEnergy(s, 20);
            return renderEnergy(s, 40);
        };
        const double flat = run([](SynthLayerConfig&) {});
        const double hp = run([](SynthLayerConfig& c) { c.eqEnabled = true; c.eqHighPass = 1000.f; c.eqHighPassSlope = 48; });
        const double hp12 = run([](SynthLayerConfig& c) { c.eqEnabled = true; c.eqHighPass = 1000.f; c.eqHighPassSlope = 12; });
        const double boost = run([](SynthLayerConfig& c) { c.eqEnabled = true; c.eqFreq[0] = 130.f; c.eqGain[0] = 12.f; c.eqQ[0] = 1.f; });
        const double off = run([](SynthLayerConfig& c) { c.eqEnabled = false; c.eqHighPass = 1000.f; c.eqHighPassSlope = 48; });
        std::printf("layer EQ: HP48 %.3f, HP12 %.3f, +12 dB band %.2f, EQ off %.3f (x flat)\n", hp / flat, hp12 / flat, boost / flat, off / flat);
        assert(hp < flat * .1 && hp < hp12 && boost > flat * 1.5 && std::abs(off / flat - 1.0) < 1e-3);
    }
    std::printf("OK v32.3 modulation matrix\n");
    return 0;
}
