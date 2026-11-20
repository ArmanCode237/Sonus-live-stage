// Assertions carry the checks: keep them in Release builds.
#undef NDEBUG
// v32.2 DX7 layer (msfa): factory cartridges parse, a DX7 layer sounds through the
// synth module, notes release and free their slot, voices survive the JSON hex form.
#include "modules/NordSynthModule.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace NordDSP;

int main(int argc, char** argv) {
    assert(argc > 1 && "pass the factory cartridge folder");
    const std::string dir = argv[1];
    std::ifstream in(dir + "/SynprezFM_01.syx", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<Dx7Voice> voices; std::string error;
    assert(parseDx7Sysex(bytes, voices, error) && voices.size() == 32);
    std::printf("cartridge SynprezFM_01: %s, %s, %s ...\n", dx7VoiceName(voices[0]).c_str(),
                dx7VoiceName(voices[1]).c_str(), dx7VoiceName(voices[2]).c_str());

    Dx7Voice back{};
    assert(dx7FromHex(dx7ToHex(voices[5]), back) && back == voices[5]);

    NordSynthModule synth;
    synth.init(44100.f);
    synth.synthEnabled = true;
    synth.synthMasterVol = .8f;
    for (auto& l : synth.layers) l.cfg.enabled = false;
    auto& layer = synth.layers[0];
    layer.cfg.enabled = true;
    layer.cfg.synthesisType = 5;
    layer.cfg.dx7Voice = voices[0];
    layer.cfg.dx7Revision = nextDx7Revision();
    for (auto& fx : layer.cfg.insertFx) fx.enabled = false;

    std::vector<float> out(2 * 256);
    double energy = 0;
    synth.noteOn(60, 60, 100);
    for (int b = 0; b < 100; ++b) {
        std::fill(out.begin(), out.end(), 0.f);
        synth.render(out.data(), 256);
        for (float x : out) { assert(std::isfinite(x)); energy += double(x) * x; }
    }
    std::printf("DX7 layer energy %.3f, voices %d\n", energy, synth.activeVoices());
    assert(energy > 1.0 && synth.activeVoices() == 1);
    synth.noteOff(60, false);
    for (int b = 0; b < 2000 && synth.activeVoices() > 0; ++b) synth.render(out.data(), 256);
    std::printf("after release: voices %d\n", synth.activeVoices());
    assert(synth.activeVoices() == 0 && "released DX7 note must free its slot");

    // LA32 synthesized partial (type 7): the cutoff shapes the waveform like the chip.
    auto brightness = [&](float cutoffHz) {
        NordSynthModule la;
        la.init(44100.f); la.synthEnabled = true; la.synthMasterVol = .8f;
        for (auto& l : la.layers) l.cfg.enabled = false;
        auto& c = la.layers[0].cfg;
        c.enabled = true; c.synthesisType = 7; c.sawLevel = 1.f; c.pulseLevel = 0.f; c.osc2Level = 0.f;
        c.cutoffHz = cutoffHz; c.filterEnvAmt = 0.f; c.resonanceQ = 0.5f;
        for (auto& fx : c.insertFx) fx.enabled = false;
        la.noteOn(57, 57, 100);
        std::vector<float> b(2 * 256);
        double e = 0, d = 0; float prev = 0;
        for (int k = 0; k < 60; ++k) {
            std::fill(b.begin(), b.end(), 0.f); la.render(b.data(), 256);
            for (size_t i = 0; i < b.size(); i += 2) {
                assert(std::isfinite(b[i]));
                if (k > 20) { e += double(b[i]) * b[i]; d += double(b[i] - prev) * (b[i] - prev); }
                prev = b[i];
            }
        }
        return std::make_pair(e, d / std::max(e, 1e-12));    // energy, high-frequency ratio
    };
    const auto dark = brightness(300.f), bright = brightness(8000.f);
    std::printf("LA32 saw A3: cutoff 300 Hz energy %.2f hf %.4f | cutoff 8 kHz energy %.2f hf %.4f\n",
                dark.first, dark.second, bright.first, bright.second);
    assert(dark.first > 0.1 && bright.first > 0.1);
    assert(bright.second > dark.second * 3 && "a higher LA32 cutoff must be brighter");
    std::printf("OK v32.2 DX7 + LA32 engines\n");
    return 0;
}
