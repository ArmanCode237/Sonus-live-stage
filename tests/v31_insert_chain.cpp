// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// v31 per-layer insert chain (synth layers and piano layer Sounds):
//  - with every unit off the signal is bit-exact (callers also skip the chain);
//  - the new REVERB unit: Room < Plate < Hall tail length, finite, no Shimmer engine;
//  - a chain with only the reverb on processes; insertTailSeconds covers its tail.
#include "dsp/LayerInsertChain.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace SonusDSP;

namespace {
constexpr float SR = 48000.f;
// Energy (dB) of the tail between 0.3 s and 1.5 s after a unit impulse.
double tailDb(int type) {
    SynthLayerInsertDSP dsp; dsp.init(SR);
    InsertFxChain fx{};
    fx[INSERT_REVERB].enabled = true; fx[INSERT_REVERB].type = type; fx[INSERT_REVERB].mix = .5f; fx[INSERT_REVERB].depth = .7f;
    double e = 0;
    for (int n = 0; n < int(SR * 1.5f); ++n) {
        float l = n == 0 ? 1.f : 0.f, r = l;
        dsp.process(l, r, fx);
        assert(std::isfinite(l) && std::isfinite(r));
        if (n > int(SR * .3f)) e += double(l) * l + double(r) * r;
    }
    return 10 * std::log10(e + 1e-30);
}
}

int main() {
    // 1. Everything off: bit-exact.
    {
        SynthLayerInsertDSP dsp; dsp.init(SR);
        InsertFxChain off{};
        assert(!insertChainActive(off));
        for (int n = 0; n < 4800; ++n) {
            const float in = std::sin(float(n) * .05f) * .5f;
            float l = in, r = -in;
            dsp.process(l, r, off);
            assert(l == in && r == -in);
        }
    }
    // 2. Reverb types.
    const double room = tailDb(0), plate = tailDb(1), hall = tailDb(2);
    std::printf("reverb tail 0.3-1.5 s: room %.1f dB, plate %.1f dB, hall %.1f dB\n", room, plate, hall);
    assert(room < plate && plate < hall);
    assert(room > -80.0);                                   // audible
    // 3. Activity and tail bookkeeping.
    InsertFxChain fx{};
    fx[INSERT_REVERB].enabled = true;
    assert(insertChainActive(fx) && insertTailSeconds(fx) >= 9.f);
    fx[INSERT_REVERB].mix = 0.f;
    assert(!insertChainActive(fx));                        // a unit at 0 % mix does nothing
    // 4. Cost: off vs one reverb (relative, for the report).
    {
        SynthLayerInsertDSP dsp; dsp.init(SR);
        InsertFxChain on{}; on[INSERT_REVERB].enabled = true; on[INSERT_REVERB].type = 2; on[INSERT_REVERB].mix = .3f;
        auto time = [&](const InsertFxChain& chain) {
            const auto t0 = std::chrono::steady_clock::now();
            float acc = 0;
            for (int n = 0; n < int(SR); ++n) { float l = .1f, r = .1f; dsp.process(l, r, chain); acc += l; }
            (void)acc;
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        };
        const double offMs = time(InsertFxChain{}), onMs = time(on);
        std::printf("1 s of audio: chain off %.2f ms, hall reverb on %.2f ms (%.2f%% of one core)\n", offMs, onMs, onMs / 10.0);
    }
    std::puts("PASS v31 insert chain");
}
