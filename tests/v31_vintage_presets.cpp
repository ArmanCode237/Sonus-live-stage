// v31 vintage factory bank (385-432): every JSON file is applied through the real
// WorkstationHost::applySynthPresetJson path, rendered offline and compared with the
// loudness of the original 384-patch bank.
//   LiveStageVintagePresetsCheck <presets/synth/factory> [--report gains.json]
// --report writes {"id": gain} multipliers (towards the reference median) for
// tools/build_vintage_presets.py --gains, instead of failing on level differences.
#include "core/WorkstationHost.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
using namespace SonusDSP;
namespace fs = std::filesystem;

namespace {
constexpr int kFirstVintage = 1;   // v32.3: the whole bank is level-checked
constexpr int kLastVintage = SYNTH_FACTORY_PRESET_COUNT;

struct Level { double loudDb; bool finite; double peak; };

// Loudest 50 ms window of a held note (C4; bass patches transpose themselves), in dBFS RMS.
Level measure(WorkstationHost& host, const json& params, int note) {
    auto& synth = host.multiSynth_;
    synth.allNotesOff();
    host.applySynthPresetJson(params);
    synth.synthEnabled = true;
    std::vector<float> block(512 * 2);
    for (int i = 0; i < 4; ++i) { std::fill(block.begin(), block.end(), 0.f); synth.render(block.data(), 512); }
    synth.noteOn(note, note, 100);
    std::vector<float> mono;
    const int blocks = int(48000 * 1.6 / 512);
    for (int b = 0; b < blocks; ++b) {
        std::fill(block.begin(), block.end(), 0.f);
        synth.render(block.data(), 512);
        for (int n = 0; n < 512; ++n) mono.push_back(.5f * (block[size_t(n) * 2] + block[size_t(n) * 2 + 1]));
    }
    synth.noteOff(note, false);
    Level out{-200.0, true, 0.0};
    for (float v : mono) { out.finite &= std::isfinite(v); out.peak = std::max(out.peak, double(std::abs(v))); }
    const size_t win = 2400;
    for (size_t start = 0; start + win <= mono.size(); start += win / 2) {
        double e = 0; for (size_t i = start; i < start + win; ++i) e += double(mono[i]) * mono[i];
        out.loudDb = std::max(out.loudDb, 10 * std::log10(e / double(win) + 1e-20));
    }
    return out;
}

json loadParams(const fs::path& file) {
    std::ifstream in(file);
    const json doc = json::parse(in);
    return doc.at("params");
}
}

int main(int argc, char** argv) {
    const fs::path dir = argc > 1 ? argv[1] : "presets/synth/factory";
    const bool report = argc > 3 && std::string(argv[2]) == "--report";
    auto file = [&](int id) { char name[16]; std::snprintf(name, sizeof name, "%03d.json", id); return dir / name; };

    WorkstationHost host;
    // Reference: every 4th patch of the original bank.
    std::vector<double> reference;
    for (int id = 1; id <= SYNTH_FACTORY_PRESET_COUNT; id += 2) {
        if (!fs::exists(file(id))) continue;
        const auto lv = measure(host, loadParams(file(id)), 60);
        if (lv.finite && lv.loudDb > -90) reference.push_back(lv.loudDb);
    }
    if (reference.size() < 20) { std::fprintf(stderr, "reference bank not found in %s\n", dir.string().c_str()); return 2; }
    std::sort(reference.begin(), reference.end());
    const double median = reference[reference.size() / 2];
    std::printf("reference median %.1f dBFS (%zu patches, range %.1f .. %.1f)\n", median, reference.size(), reference.front(), reference.back());

    int failures = 0;
    std::map<int, double> gains;
    for (int id = kFirstVintage; id <= kLastVintage; ++id) {
        if (!fs::exists(file(id))) { std::printf("MISSING %d\n", id); ++failures; continue; }
        std::ifstream in(file(id));
        const json doc = json::parse(in);
        const auto lv = measure(host, doc.at("params"), 60);
        const double diff = lv.loudDb - median;
        const bool ok = lv.finite && lv.loudDb > -60 && lv.peak < 4.0 && (report || std::abs(diff) <= 4.0);
        std::printf("%s %3d %-34s %6.1f dB (%+5.1f) peak %.2f\n", ok ? "ok  " : "FAIL", id,
                    doc.value("name", std::string()).c_str(), lv.loudDb, diff, lv.peak);
        if (!ok) ++failures;
        // Loaded state must match the file (catches stale range clamps on the load path).
        const auto& want = doc["params"]["layers"][0];
        const auto& got = host.multiSynth_.layers[0].cfg;
        const bool same = got.synthesisType == want.value("synthesisType", -1) && got.waveType == want.value("waveType", -1) &&
            got.filterType == want.value("filterType", -1) && got.fm4Algorithm == want.value("fm4Algorithm", -1) &&
            got.transientType == want.value("transientType", -1) && std::abs(got.analog - want.value("analog", -1.f)) < 1e-4f;
        if (!same) { std::printf("FAIL %d layer A fields not applied as stored\n", id); ++failures; }
        gains[id] = std::pow(10.0, -diff / 20.0);
    }
    if (report) {
        json out = json::object();
        for (const auto& [id, g] : gains) out[std::to_string(id)] = std::round(g * 1000.0) / 1000.0;
        std::ofstream(argv[3]) << out.dump(1);
        std::printf("gains written to %s\n", argv[3]);
        return failures ? 1 : 0;
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("PASS v31 vintage presets");
}
