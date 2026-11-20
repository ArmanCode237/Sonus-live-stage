// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// v31 control mappings: scopes and precedence, one source → many parameters, one
// assignment per parameter and scope (MIDI + OSC together), conflicts with SYSTEM,
// pickup / jump / relative, toggle / momentary, note sources, OSC, learn, migration.
#include "control/ControlMappings.h"
#include "control/ParamValueReader.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
using namespace SonusDSP;
using json = nlohmann::json;

namespace {
std::map<std::string, float> values;
int writes = 0;
void param(ParameterRegistry& r, const std::string& id, float lo, float hi, float initial) {
    values[id] = initial;
    r.registerParam(id, lo, hi, [id](float v) { values[id] = v; ++writes; });
}
bool near(float a, float b, float tol = 1e-3f) { return std::abs(a - b) <= tol; }
ControlAssignment cc(const std::string& id, MapScope scope, int number, int channel = 0, int module = -1) {
    ControlAssignment a; a.paramId = id; a.scope = scope; a.module = module;
    a.midi = {MidiKind::CC, channel, number};
    return a;
}
}

int main() {
    ParameterRegistry registry;
    param(registry, "L1_cut", 40.f, 20000.f, 8000.f);
    param(registry, "L1_en", 0.f, 1.f, 1.f);
    param(registry, "volume", 0.f, 1.5f, .75f);
    param(registry, "organDrive", 0.f, 1.f, .2f);
    param(registry, "pianoVolume", 0.f, 1.5f, 1.f);
    ControlMappings m(registry);
    registry.setWriteObserver([&m](const std::string& id, float v) { m.noteWritten(id, v); });
    // Same shape as serializeParams(): synth layer fields are nested.
    auto full = [&]() {
        json p = {{"volume", values["volume"]}, {"organDrive", values["organDrive"]}, {"pianoVolume", values["pianoVolume"]}};
        p["synthLayers"] = json::array({{{"enabled", values["L1_en"] >= .5f}, {"cutoffHz", values["L1_cut"]}}});
        return p;
    };
    std::string error;

    // ---- module classification
    assert(parameterModule("L3_fm4Algo") == 1 && parameterModule("synthFxReverbMix") == 1 && parameterModule("fxSend6Mod") == 1);
    assert(parameterModule("drawbar3") == 2 && parameterModule("organDrive") == 2 && parameterModule("fxSend9Delay") == 2);
    assert(parameterModule("pianoSample2Atk") == 0 && parameterModule("studioSlot0_1_p3") == 0 && parameterModule("fxSend4Reverb") == 0);
    assert(parameterModule("volume") == -1 && parameterModule("studioSlot3_0_p0") == -1 && parameterModule("programSlot01") == -1);

    // ---- validation: MODULE scope only accepts that module's parameters
    assert(!m.assign(cc("volume", MapScope::Module, 7, 0, 0), &error));
    assert(!m.assign(cc("L1_cut", MapScope::Module, 20, 0, 2), &error));
    assert(!ControlAssignment::fromJson(json{{"paramId", "volume"}}, registry, MapScope::Module, -1, &error));

    // ---- precedence: PROGRAM > MODULE > SYSTEM for the same parameter
    auto jump = [](ControlAssignment a) { a.takeover = Takeover::Jump; return a; };
    assert(m.assign(jump(cc("L1_cut", MapScope::System, 74)), &error));
    assert(m.assign(jump(cc("L1_cut", MapScope::Module, 20, 0, 1)), &error));
    m.handleMidi(MidiKind::CC, 1, 74, 127);
    assert(near(values["L1_cut"], 8000.f));                 // SYSTEM shadowed by MODULE
    m.handleMidi(MidiKind::CC, 1, 20, 127);
    assert(near(values["L1_cut"], 20000.f));
    assert(m.assign(jump(cc("L1_cut", MapScope::Program, 21)), &error));
    m.handleMidi(MidiKind::CC, 1, 20, 0);
    assert(near(values["L1_cut"], 20000.f));                // MODULE shadowed by PROGRAM
    m.handleMidi(MidiKind::CC, 1, 21, 0);
    assert(near(values["L1_cut"], 40.f));
    m.remove("L1_cut", MapScope::Program);
    m.handleMidi(MidiKind::CC, 1, 20, 127);
    assert(near(values["L1_cut"], 20000.f));                // MODULE is effective again

    // ---- one assignment per parameter and scope: assigning again replaces it
    assert(m.assign(jump(cc("L1_cut", MapScope::Module, 22, 0, 1)), &error));
    m.handleMidi(MidiKind::CC, 1, 20, 0);
    assert(near(values["L1_cut"], 20000.f));
    // ---- one source → several parameters (here across modules)
    assert(m.assign(jump(cc("organDrive", MapScope::Module, 22, 0, 2)), &error));
    m.handleMidi(MidiKind::CC, 1, 22, 0);
    assert(near(values["L1_cut"], 40.f) && near(values["organDrive"], 0.f));

    // ---- MIDI and OSC on the same assignment; removing one part keeps the other
    {
        auto a = *m.get("organDrive", MapScope::Module);
        a.osc = "/live/drive";
        assert(m.assign(a, &error));
        assert(m.handleOsc("/live/drive", .5f) && near(values["organDrive"], .5f));
        assert(m.remove("organDrive", MapScope::Module, true, false));
        m.handleMidi(MidiKind::CC, 1, 22, 127);              // still moves L1_cut, no longer the drive
        assert(near(values["organDrive"], .5f) && near(values["L1_cut"], 20000.f));
        assert(m.handleOsc("/live/drive", 1.f) && near(values["organDrive"], 1.f));
        assert(m.get("organDrive", MapScope::Module) && !m.get("organDrive", MapScope::Module)->midi.valid());
    }

    // ---- conflicts: a source used by SYSTEM and by a preset / program
    assert(m.assign(cc("volume", MapScope::System, 7), &error));
    assert(m.conflicts().empty());
    assert(m.assign(cc("pianoVolume", MapScope::Module, 7, 2, 0), &error));    // channel 2 overlaps omni
    {
        const json c = m.conflicts();
        assert(c.size() == 1);
        assert(c[0]["source"] == "CC 7 · canal 2" || c[0]["source"] == "CC 7 · omni");
        assert(c[0]["system"][0]["paramId"] == "volume" && c[0]["others"][0]["paramId"] == "pianoVolume");
    }
    m.remove("pianoVolume", MapScope::Module);
    assert(m.conflicts().empty());

    // ---- pickup: the knob must reach the current value before it takes over
    m.syncValues(full());
    writes = 0;
    m.handleMidi(MidiKind::CC, 1, 7, 0);                    // volume .75 = 63.5 / 127 of 1.5
    m.handleMidi(MidiKind::CC, 1, 7, 20);
    assert(writes == 0 && near(values["volume"], .75f));
    m.handleMidi(MidiKind::CC, 1, 7, 70);                   // crosses .75 → picked up
    assert(writes == 1 && near(values["volume"], 1.5f * 70 / 127));
    m.handleMidi(MidiKind::CC, 1, 7, 10);
    assert(near(values["volume"], 1.5f * 10 / 127));        // follows freely once picked up
    registry.setParamValue("volume", 1.2f);                 // the UI moves it: pickup re-arms
    writes = 0;
    m.handleMidi(MidiKind::CC, 1, 7, 12);
    assert(writes == 0);

    // ---- toggle and momentary buttons, note sources
    {
        ControlAssignment t; t.paramId = "L1_en"; t.scope = MapScope::Program; t.midi = {MidiKind::Note, 10, 36}; t.button = ButtonMode::Toggle;
        assert(m.assign(t, &error));
        m.syncValues(full());
        assert(m.handleMidi(MidiKind::Note, 10, 36, 100) && near(values["L1_en"], 0.f));
        assert(m.handleMidi(MidiKind::Note, 10, 36, 0) && near(values["L1_en"], 0.f));
        m.handleMidi(MidiKind::Note, 10, 36, 90);
        assert(near(values["L1_en"], 1.f));
        assert(!m.handleMidi(MidiKind::Note, 1, 36, 90));   // other channel: plays normally
        t.button = ButtonMode::Momentary; assert(m.assign(t, &error));
        m.handleMidi(MidiKind::Note, 10, 36, 100); assert(near(values["L1_en"], 1.f));
        m.handleMidi(MidiKind::Note, 10, 36, 0);   assert(near(values["L1_en"], 0.f));
    }

    // ---- relative encoders and invert
    {
        auto a = cc("organDrive", MapScope::Module, 30, 0, 2); a.takeover = Takeover::Relative; a.osc.clear();
        assert(m.assign(a, &error));
        values["organDrive"] = .5f; m.syncValues(full());
        m.handleMidi(MidiKind::CC, 1, 30, 1);     assert(near(values["organDrive"], .5f + 1.f / 128));
        m.handleMidi(MidiKind::CC, 1, 30, 126);   assert(near(values["organDrive"], .5f - 1.f / 128));
        auto inv = jump(cc("pianoVolume", MapScope::Module, 31, 0, 0)); inv.invert = true;
        assert(m.assign(inv, &error));
        m.handleMidi(MidiKind::CC, 1, 31, 127);   assert(near(values["pianoVolume"], 0.f));
        m.handleMidi(MidiKind::CC, 1, 31, 0);     assert(near(values["pianoVolume"], 1.5f));
    }

    // ---- learn: wheel, sustain and channel-mode CCs are ignored unless allowed
    m.armLearn();
    for (int number : {1, 64, 120, 123, 127}) m.handleMidi(MidiKind::CC, 1, number, 0);
    assert(m.learnState()["active"] == true);
    m.handleMidi(MidiKind::Note, 3, 40, 0);                 // a note-off is not a gesture
    assert(m.learnState()["active"] == true);
    m.handleMidi(MidiKind::CC, 5, 74, 30);
    {
        const json s = m.learnState();
        assert(s["active"] == false && s["captured"]["number"] == 74 && s["captured"]["channel"] == 5 && s["captured"]["kind"] == "cc");
    }
    m.armLearn(true);
    m.handleMidi(MidiKind::CC, 1, 1, 10);
    assert(m.learnState()["captured"]["number"] == 1);
    m.armLearn();
    m.handleOsc("/fader/3", .2f);
    assert(m.learnState()["captured"]["address"] == "/fader/3");

    // ---- export / replace (preset save → load) and legacy migration
    {
        const json saved = m.exportScope(MapScope::Module, 2);
        m.replaceScope(MapScope::Module, 2, json::array());
        assert(m.exportScope(MapScope::Module, 2).empty());
        m.replaceScope(MapScope::Module, 2, saved);
        assert(m.exportScope(MapScope::Module, 2) == saved);
        const json legacyMidi = json::array({{{"cc", 7}, {"channel", 0}, {"paramId", "volume"}}, {{"cc", 8}, {"channel", 0}, {"paramId", "volume"}},
                                             {{"cc", 9}, {"paramId", "doesNotExist"}}});
        const json migrated = ControlMappings::migrateLegacyMidi(legacyMidi, registry, MapScope::System, -1);
        assert(migrated.size() == 1 && migrated[0]["midi"]["number"] == 7);   // first wins, unknown dropped
        json merged = migrated;
        for (const auto& r : ControlMappings::migrateLegacyOsc(json::array({{{"address", "/vol"}, {"paramId", "volume"}}}), registry, MapScope::System, -1))
            merged.push_back(r);
        m.replaceScope(MapScope::System, -1, merged);
        const auto vol = m.get("volume", MapScope::System);
        assert(vol && vol->midi.number == 7 && vol->osc == "/vol");          // MIDI + OSC merged
    }
    // ---- SOUND scope (piano layer Sounds): layer-only, retargetable, below PERFORMANCE
    {
        param(registry, "pianoSample0Cut", 40.f, 20000.f, 9000.f);
        param(registry, "pianoSample3Cut", 40.f, 20000.f, 9000.f);
        param(registry, "pianoLayerVol0", 0.f, 1.5f, 1.f);
        param(registry, "pianoFx1_5_mix", 0.f, 1.f, .3f);
        assert(soundLayerOfParam("pianoSample0Cut") == 0 && soundLayerOfParam("pianoFx1_5_mix") == 1);
        assert(soundLayerOfParam("studioSend0_4_2") == 4 && soundLayerOfParam("fxSend2Reverb") == 2);
        assert(soundLayerOfParam("pianoLayerVol0") == -1 && soundLayerOfParam("fxSend6Mod") == -1);   // performance / synth
        assert(retargetSoundParam("pianoSample0Cut", 3) == "pianoSample3Cut");
        assert(retargetSoundParam("pianoFx1_5_mix", 4) == "pianoFx4_5_mix");
        assert(retargetSoundParam("studioSend0_2_4", 0) == "studioSend0_0_4");
        assert(retargetSoundParam("volume", 2) == "volume");

        ControlAssignment s0; s0.paramId = "pianoSample0Cut"; s0.scope = MapScope::Sound; s0.midi = {MidiKind::CC, 0, 40}; s0.takeover = Takeover::Jump;
        assert(m.assign(s0, &error));
        ControlAssignment bad = s0; bad.paramId = "pianoLayerVol0";
        assert(!m.assign(bad, &error));                     // layer volume belongs to the performance
        m.handleMidi(MidiKind::CC, 1, 40, 0);
        assert(near(values["pianoSample0Cut"], 40.f));
        // SYSTEM below SOUND, PERFORMANCE above it
        auto sys = s0; sys.scope = MapScope::System; sys.midi.number = 42; assert(m.assign(sys, &error));
        m.handleMidi(MidiKind::CC, 1, 42, 127);
        assert(near(values["pianoSample0Cut"], 40.f));      // SYSTEM shadowed by SOUND
        auto perf = s0; perf.scope = MapScope::Module; perf.module = 0; perf.midi.number = 41; assert(m.assign(perf, &error));
        m.handleMidi(MidiKind::CC, 1, 40, 127);
        assert(near(values["pianoSample0Cut"], 40.f));      // SOUND shadowed by PERFORMANCE
        m.handleMidi(MidiKind::CC, 1, 41, 127);
        assert(near(values["pianoSample0Cut"], 20000.f));
        m.remove("pianoSample0Cut", MapScope::Module);
        m.remove("pianoSample0Cut", MapScope::System);
        // A Sound saved on layer 0 and loaded on layer 3 drives layer 3
        json saved = m.exportScope(MapScope::Sound, 0);
        assert(saved.size() == 1 && saved[0]["layer"] == 0 && saved[0]["scope"] == "sound");
        for (auto& item : saved) { item["paramId"] = retargetSoundParam(item["paramId"], 3); item["layer"] = 3; }
        m.replaceScope(MapScope::Sound, 3, saved);
        m.handleMidi(MidiKind::CC, 1, 40, 0);
        assert(near(values["pianoSample3Cut"], 40.f));
        assert(m.get("pianoSample3Cut", MapScope::Sound).has_value());
        // conflicts with SYSTEM report SOUND assignments with their layer
        auto sys2 = cc("volume", MapScope::System, 40); assert(m.assign(sys2, &error));
        bool found = false;
        for (const auto& c : m.conflicts()) for (const auto& o : c["others"]) found |= o["scope"] == "sound" && o["layer"] == 3;
        assert(found);
    }
    std::puts("PASS v31 control mappings");
}
