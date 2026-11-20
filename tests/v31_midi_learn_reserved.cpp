// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// v31: MIDI learn must not capture the mod wheel (CC1), sustain (CC64) or channel-mode
// messages (CC120-127). Moving the wheel while a learn is armed used to bind CC1 to the
// target (e.g. L1_sus), so returning the wheel to 0 silenced the synth layer.
#include "control/ControlMappings.h"
#include <cassert>
#include <cstdio>

int main() {
    SonusDSP::ParameterRegistry registry;
    float sustain = .7f;
    registry.registerParam("L1_sus", 0.f, 1.f, [&](float v) { sustain = v; });
    SonusDSP::ControlMappings mappings(registry);

    mappings.armLearn();
    for (int cc : {1, 64, 120, 123, 127}) {
        mappings.handleMidi(SonusDSP::MidiKind::CC, 1, cc, 0);
        assert(mappings.learnState()["active"] == true);   // still waiting for a real control
        assert(sustain == .7f);                            // and nothing was written
    }
    mappings.handleMidi(SonusDSP::MidiKind::CC, 1, 74, 127);  // a real knob
    const auto state = mappings.learnState();
    assert(state["active"] == false && state["captured"]["number"] == 74);
    std::puts("PASS v31 MIDI learn ignores CC1 / CC64 / CC120-127");
}
