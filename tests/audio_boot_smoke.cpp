// Regression: RtAudio is allowed to invoke its callback as soon as it starts.
// The engine must be safe to render even before the hardware manager runs.
#include "../src/modules/StagePianoEngine.h"
#include <array>
#include <cmath>
#include <iostream>
int main() {
    SonusDSP::StagePianoEngine e;
    std::array<float, 256> out{};
    e.render(out.data(), 128);
    for (float sample : out) if (!std::isfinite(sample)) return 2;
    e.setSampleRate(44100.0);
    e.render(out.data(), 128);
    for (float sample : out) if (!std::isfinite(sample)) return 3;
    std::cout << "OK: callback seguro antes y despues de configurar el dispositivo.\n";
    return 0;
}
