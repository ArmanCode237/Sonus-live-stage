#pragma once
// MIDI / OSC control mappings with three scopes.
//
//   SYSTEM   persistent (control_mappings.json): hardware that never changes
//   SOUND    stored with each piano layer Sound; one set per piano layer, only that
//            layer's Sound parameters; retargeted when the Sound moves to another layer
//   MODULE   stored with each module preset (synth preset, piano performance,
//            organ preset); one set per module, only that module's parameters
//   PROGRAM  stored with each global program; any parameter
//
// Rules:
//   - per scope, a parameter has at most one assignment, with one MIDI source and
//     one OSC address at the same time;
//   - for the same parameter: PROGRAM > MODULE (performance / preset) > SOUND > SYSTEM;
//   - one MIDI message / OSC address may drive several parameters;
//   - a source used both by SYSTEM and by a preset/program is reported as a conflict.
//
// Threads: editing runs on HTTP threads under a mutex; MIDI and OSC threads read an
// immutable snapshot (atomic shared_ptr) and never wait on edits.
#include "../core/ParameterRegistry.h"
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace SonusDSP {

enum class MapScope : int { System = 0, Module = 1, Program = 2, Sound = 3 };
enum class MidiKind : int { CC = 0, Note = 1 };
enum class Takeover : int { Pickup = 0, Jump = 1, Relative = 2 };
enum class ButtonMode : int { Continuous = 0, Toggle = 1, Momentary = 2 };

struct MidiSource {
    MidiKind kind = MidiKind::CC;
    int channel = 0;   // 0 = omni, 1-16
    int number = -1;   // -1 = none
    std::string port;  // v33 (M4): MIDI input port name; empty = any device
    bool valid() const { return number >= 0 && number < 128 && channel >= 0 && channel <= 16; }
    bool overlaps(const MidiSource& o) const {
        return valid() && o.valid() && kind == o.kind && number == o.number &&
               (channel == 0 || o.channel == 0 || channel == o.channel) &&
               (port.empty() || o.port.empty() || port == o.port);
    }
    std::string label() const;
};

struct ControlAssignment {
    std::string paramId;
    MapScope scope = MapScope::System;
    int module = -1;                // owning module for MODULE scope
    int layer = -1;                 // piano layer for SOUND scope
    MidiSource midi;                // number -1: no MIDI source
    std::string osc;                // empty: no OSC address
    // Parameter units. NaN = the parameter's full range (resolved by ControlMappings::assign).
    float min = std::numeric_limits<float>::quiet_NaN(), max = std::numeric_limits<float>::quiet_NaN();
    bool invert = false;
    Takeover takeover = Takeover::Pickup;
    ButtonMode button = ButtonMode::Continuous;

    nlohmann::json toJson() const;
    /** Parses an assignment. Missing range fields take the parameter's full range. */
    static std::optional<ControlAssignment> fromJson(const nlohmann::json& j, const ParameterRegistry& registry,
                                                     MapScope scope, int module, std::string* error = nullptr);
};

std::string scopeName(MapScope scope);
std::optional<MapScope> parseScope(const std::string& name);
/** Valid OSC address for a mapping: "/...", no spaces, at most 160 characters. */
bool validOscAddress(const std::string& address);

class ControlMappings {
public:
    explicit ControlMappings(ParameterRegistry& registry);

    // ---- editing (HTTP threads) -------------------------------------------------
    /** Inserts or replaces the assignment of a.paramId in a.scope. */
    bool assign(const ControlAssignment& a, std::string* error = nullptr);
    std::optional<ControlAssignment> get(const std::string& paramId, MapScope scope) const;
    /** Removes the MIDI and/or OSC part; the assignment disappears when both are gone. */
    bool remove(const std::string& paramId, MapScope scope, bool midi = true, bool osc = true);
    /** MODULE scope: module 0-2. SOUND scope: piano layer 0-4. SYSTEM / PROGRAM ignore it. */
    nlohmann::json exportScope(MapScope scope, int module = -1) const;
    /** Replaces a whole scope (preset / program load). Invalid entries are skipped. */
    void replaceScope(MapScope scope, int module, const nlohmann::json& assignments);
    /** Every assignment of every scope with its effective / shadowed state, plus conflicts. */
    nlohmann::json describe() const;
    /** MIDI messages / OSC addresses used both by SYSTEM and by a preset or program. */
    nlohmann::json conflicts() const;
    uint64_t revision() const { return revision_.load(); }

    /** Re-reads the current value of every mapped parameter (after preset/program loads). */
    void syncValues(const nlohmann::json& serializedParams);

    // ---- runtime (MIDI / OSC threads) --------------------------------------------
    /** channel 1-16, value 0-127 (note off = 0). Returns true when a mapping consumed it. */
    bool handleMidi(MidiKind kind, int channel, int number, int value, int source = 0);
    /** v33 (M4): names of the MIDI sources (index = source id) for device-specific mappings. */
    void setSourceNames(const std::array<std::string, 16>& names);
    /** value normalised 0..1. Returns true when a mapping consumed it. */
    bool handleOsc(const std::string& address, float value);
    /** v32.4 OSC feedback: every OSC-mapped parameter with a known value, as 0..1 in its
     *  mapped range (inverted if the mapping is). Lock-free read of the current snapshot. */
    struct OscFeedback { std::string address; float value01; };
    std::vector<OscFeedback> oscFeedback() const;
    /** Registry hook: any write (UI, OSC, MIDI). Keeps pickup in step with the UI. */
    void noteWritten(const std::string& paramId, float value);
    /** Called after a mapping wrote a parameter (dirty tracking). */
    void setWriteListener(std::function<void(const std::string&)> listener) { writeListener_ = std::move(listener); }

    // ---- learn ---------------------------------------------------------------------
    /** Captures the next MIDI message or OSC address. The wheel (CC1), sustain (CC64)
     *  and channel-mode CCs (120-127) are ignored unless allowReserved. */
    void armLearn(bool allowReserved = false);
    void cancelLearn();
    nlohmann::json learnState() const;
    nlohmann::json lastInput() const;

    static nlohmann::json migrateLegacyMidi(const nlohmann::json& bindings, const ParameterRegistry& registry,
                                            MapScope scope, int module);
    static nlohmann::json migrateLegacyOsc(const nlohmann::json& routes, const ParameterRegistry& registry,
                                           MapScope scope, int module);

private:
    struct Entry {
        ControlAssignment a;
        mutable std::atomic<float> current{0.f};
        mutable std::atomic<bool> known{false};
        mutable std::atomic<bool> picked{false};
        mutable std::atomic<int> lastRaw{-1};
    };
    struct Snapshot {
        std::vector<std::unique_ptr<Entry>> entries;
        std::unordered_map<int, std::vector<const Entry*>> midi;
        std::unordered_map<std::string, std::vector<const Entry*>> osc;
        std::unordered_map<std::string, const Entry*> byParam;
    };
    using ScopeMap = std::map<std::string, ControlAssignment>;

    static int midiKey(MidiKind kind, int channel, int number) { return int(kind) * 4096 + channel * 128 + number; }
    ScopeMap& scopeMap(MapScope scope, int module);
    const ScopeMap* scopeMapPtr(MapScope scope, int module) const;
    std::vector<const ControlAssignment*> effectiveLocked() const;
    void rebuildLocked();
    void apply(const Entry& e, int value) const;
    void write(const Entry& e, float value) const;

    ParameterRegistry& registry_;
    mutable std::mutex mutex_;
    ScopeMap system_, program_;
    std::array<ScopeMap, 3> module_;
    std::array<ScopeMap, 5> sound_;
    std::unordered_map<std::string, float> values_;
    std::shared_ptr<const Snapshot> snapshot_;
    std::shared_ptr<const std::array<std::string, 16>> sourceNames_ = std::make_shared<const std::array<std::string, 16>>();
    std::atomic<uint64_t> revision_{0};
    std::function<void(const std::string&)> writeListener_;

    mutable std::mutex learnMutex_;
    std::atomic<bool> learnActive_{false};
    bool learnAllowReserved_ = false;
    nlohmann::json learnCaptured_;
    uint64_t learnRevision_ = 0;
    // v32.4: the MIDI thread records the last input lock-free (no JSON, no mutex per
    // note): seq << 32 | kind << 24 | channel << 16 | number << 8 | value. OSC keeps a
    // JSON under the mutex; lastInput() returns whichever came last.
    std::atomic<uint64_t> lastMidi_{0};
    std::atomic<uint32_t> inputSeq_{0};
    uint32_t lastOscSeq_ = 0;
    nlohmann::json lastOsc_;
};

} // namespace SonusDSP
