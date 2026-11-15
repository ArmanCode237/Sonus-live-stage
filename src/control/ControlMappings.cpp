#include "ControlMappings.h"
#include "ParamValueReader.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace NordDSP {
using json = nlohmann::json;

namespace {
const char* kindName(MidiKind k) { return k == MidiKind::Note ? "note" : "cc"; }
const char* takeoverName(Takeover t) { return t == Takeover::Jump ? "jump" : t == Takeover::Relative ? "relative" : "pickup"; }
const char* buttonName(ButtonMode b) { return b == ButtonMode::Toggle ? "toggle" : b == ButtonMode::Momentary ? "momentary" : "continuous"; }
bool reservedForLearn(MidiKind kind, int number) { return kind == MidiKind::CC && (number == 1 || number == 64 || number >= 120); }
}

std::string MidiSource::label() const {
    if (!valid()) return {};
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    std::string text = kind == MidiKind::Note
        ? std::string("NOTE ") + names[number % 12] + std::to_string(number / 12 - 1)
        : "CC " + std::to_string(number);
    return text + (channel == 0 ? " · omni" : " · canal " + std::to_string(channel));
}

std::string scopeName(MapScope scope) {
    switch (scope) {
        case MapScope::Program: return "program";
        case MapScope::Module: return "module";
        case MapScope::Sound: return "sound";
        default: return "system";
    }
}
std::optional<MapScope> parseScope(const std::string& name) {
    if (name == "system") return MapScope::System;
    if (name == "module") return MapScope::Module;
    if (name == "program") return MapScope::Program;
    if (name == "sound") return MapScope::Sound;
    return std::nullopt;
}
bool validOscAddress(const std::string& a) {
    return a.size() >= 2 && a.size() <= 160 && a[0] == '/' && a.find_first_of(" \t#*,?[]{}") == std::string::npos;
}

json ControlAssignment::toJson() const {
    json j = {{"paramId", paramId}, {"scope", scopeName(scope)}, {"min", min}, {"max", max}, {"invert", invert},
              {"takeover", takeoverName(takeover)}, {"button", buttonName(button)}};
    if (scope == MapScope::Module) j["module"] = module;
    if (scope == MapScope::Sound) j["layer"] = layer;
    j["midi"] = midi.valid() ? json{{"type", kindName(midi.kind)}, {"channel", midi.channel}, {"number", midi.number}} : json(nullptr);
    j["osc"] = osc.empty() ? json(nullptr) : json(osc);
    return j;
}

std::optional<ControlAssignment> ControlAssignment::fromJson(const json& j, const ParameterRegistry& registry,
                                                             MapScope scope, int module, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<ControlAssignment> { if (error) *error = why; return std::nullopt; };
    if (!j.is_object()) return fail("Asignación inválida");
    ControlAssignment a;
    a.paramId = j.value("paramId", std::string{});
    float lo = 0, hi = 1;
    if (!registry.paramRange(a.paramId, lo, hi)) return fail("Parámetro desconocido");
    a.scope = scope;
    const int owner = parameterModule(a.paramId);
    if (scope == MapScope::Module) {
        if (owner < 0) return fail("Este parámetro es global: asígnalo en Programa o Sistema");
        if (module >= 0 && module != owner) return fail("El parámetro pertenece a otro módulo");
        a.module = owner;
    }
    if (scope == MapScope::Sound) {
        const int layer = soundLayerOfParam(a.paramId);
        if (layer < 0) return fail("Solo los parámetros del sonido de una capa de piano pueden mapearse en el sonido");
        if (module >= 0 && module != layer) return fail("El parámetro pertenece a otra capa");
        a.layer = layer;
    }
    if (j.contains("midi") && j["midi"].is_object()) {
        const auto& m = j["midi"];
        a.midi.kind = m.value("type", std::string("cc")) == "note" ? MidiKind::Note : MidiKind::CC;
        a.midi.channel = m.value("channel", 0);
        a.midi.number = m.value("number", -1);
        if (!a.midi.valid()) return fail("Mensaje MIDI inválido");
    }
    if (j.contains("osc") && j["osc"].is_string()) {
        a.osc = j["osc"].get<std::string>();
        if (!a.osc.empty() && !validOscAddress(a.osc)) return fail("Dirección OSC inválida");
    }
    auto num = [&](const char* key, float fallback) {
        return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
    };
    a.min = std::clamp(num("min", lo), lo, hi);
    a.max = std::clamp(num("max", hi), lo, hi);
    a.invert = j.value("invert", false);
    const auto t = j.value("takeover", std::string("pickup"));
    a.takeover = t == "jump" ? Takeover::Jump : t == "relative" ? Takeover::Relative : Takeover::Pickup;
    const auto b = j.value("button", std::string("continuous"));
    a.button = b == "toggle" ? ButtonMode::Toggle : b == "momentary" ? ButtonMode::Momentary : ButtonMode::Continuous;
    return a;
}

ControlMappings::ControlMappings(ParameterRegistry& registry) : registry_(registry) {
    std::atomic_store(&snapshot_, std::shared_ptr<const Snapshot>(std::make_shared<Snapshot>()));
}

ControlMappings::ScopeMap& ControlMappings::scopeMap(MapScope scope, int module) {
    if (scope == MapScope::Program) return program_;
    if (scope == MapScope::Module) return module_[size_t(std::clamp(module, 0, 2))];
    if (scope == MapScope::Sound) return sound_[size_t(std::clamp(module, 0, 4))];
    return system_;
}
const ControlMappings::ScopeMap* ControlMappings::scopeMapPtr(MapScope scope, int module) const {
    if (scope == MapScope::Program) return &program_;
    if (scope == MapScope::Module) return module >= 0 && module <= 2 ? &module_[size_t(module)] : nullptr;
    if (scope == MapScope::Sound) return module >= 0 && module <= 4 ? &sound_[size_t(module)] : nullptr;
    return &system_;
}

bool ControlMappings::assign(const ControlAssignment& input, std::string* error) {
    float lo, hi;
    if (!registry_.paramRange(input.paramId, lo, hi)) { if (error) *error = "Parámetro desconocido"; return false; }
    ControlAssignment a = input;
    a.min = std::isnan(a.min) ? lo : std::clamp(a.min, lo, hi);
    a.max = std::isnan(a.max) ? hi : std::clamp(a.max, lo, hi);
    if (!a.midi.valid() && a.osc.empty()) { if (error) *error = "Falta el mensaje MIDI o la ruta OSC"; return false; }
    if (a.scope == MapScope::Module && (a.module < 0 || a.module > 2 || parameterModule(a.paramId) != a.module)) {
        if (error) *error = "Ámbito de módulo inválido para este parámetro";
        return false;
    }
    if (a.scope == MapScope::Sound) {
        a.layer = soundLayerOfParam(a.paramId);
        if (a.layer < 0) { if (error) *error = "El parámetro no pertenece al sonido de una capa"; return false; }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    scopeMap(a.scope, a.scope == MapScope::Sound ? a.layer : a.module)[a.paramId] = a;
    rebuildLocked();
    return true;
}

std::optional<ControlAssignment> ControlMappings::get(const std::string& paramId, MapScope scope) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto* map = scopeMapPtr(scope, scope == MapScope::Module ? parameterModule(paramId)
                                         : scope == MapScope::Sound ? soundLayerOfParam(paramId) : -1);
    if (!map) return std::nullopt;
    const auto it = map->find(paramId);
    if (it == map->end()) return std::nullopt;
    return it->second;
}

bool ControlMappings::remove(const std::string& paramId, MapScope scope, bool midi, bool osc) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int module = scope == MapScope::Module ? parameterModule(paramId)
                     : scope == MapScope::Sound ? soundLayerOfParam(paramId) : -1;
    if ((scope == MapScope::Module || scope == MapScope::Sound) && module < 0) return false;
    auto& map = scopeMap(scope, module);
    const auto it = map.find(paramId);
    if (it == map.end()) return false;
    if (midi) it->second.midi = MidiSource{};
    if (osc) it->second.osc.clear();
    if (!it->second.midi.valid() && it->second.osc.empty()) map.erase(it);
    rebuildLocked();
    return true;
}

json ControlMappings::exportScope(MapScope scope, int module) const {
    std::lock_guard<std::mutex> lock(mutex_);
    json out = json::array();
    if (const auto* map = scopeMapPtr(scope, module))
        for (const auto& [id, a] : *map) out.push_back(a.toJson());
    return out;
}

void ControlMappings::replaceScope(MapScope scope, int module, const json& assignments) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& map = scopeMap(scope, module);
    map.clear();
    if (assignments.is_array())
        for (const auto& item : assignments) {
            auto a = ControlAssignment::fromJson(item, registry_, scope, module);
            if (!a || (!a->midi.valid() && a->osc.empty())) continue;
            // Legacy files list MIDI and OSC separately: merge them into one assignment.
            if (auto it = map.find(a->paramId); it != map.end()) {
                if (!a->midi.valid()) a->midi = it->second.midi;
                if (a->osc.empty()) a->osc = it->second.osc;
            }
            map[a->paramId] = *a;
        }
    rebuildLocked();
}

std::vector<const ControlAssignment*> ControlMappings::effectiveLocked() const {
    std::set<std::string> ids;
    for (const auto& [id, a] : system_) ids.insert(id);
    for (const auto& [id, a] : program_) ids.insert(id);
    for (const auto& m : module_) for (const auto& [id, a] : m) ids.insert(id);
    for (const auto& m : sound_) for (const auto& [id, a] : m) ids.insert(id);
    std::vector<const ControlAssignment*> out;
    for (const auto& id : ids) {
        // PROGRAM > MODULE (performance / preset) > SOUND > SYSTEM
        if (auto it = program_.find(id); it != program_.end()) { out.push_back(&it->second); continue; }
        const int mod = parameterModule(id);
        if (mod >= 0) if (auto it = module_[size_t(mod)].find(id); it != module_[size_t(mod)].end()) { out.push_back(&it->second); continue; }
        const int layer = soundLayerOfParam(id);
        if (layer >= 0) if (auto it = sound_[size_t(layer)].find(id); it != sound_[size_t(layer)].end()) { out.push_back(&it->second); continue; }
        if (auto it = system_.find(id); it != system_.end()) out.push_back(&it->second);
    }
    return out;
}

void ControlMappings::rebuildLocked() {
    auto next = std::make_shared<Snapshot>();
    const auto previous = std::atomic_load(&snapshot_);
    for (const auto* a : effectiveLocked()) {
        auto e = std::make_unique<Entry>();
        e->a = *a;
        // Keep the known value across edits so pickup does not re-arm on every change.
        if (auto old = previous->byParam.find(a->paramId); old != previous->byParam.end() && old->second->known.load()) {
            e->current.store(old->second->current.load()); e->known.store(true);
        } else if (auto v = values_.find(a->paramId); v != values_.end()) {
            e->current.store(v->second); e->known.store(true);
        }
        const Entry* raw = e.get();
        if (a->midi.valid()) next->midi[midiKey(a->midi.kind, a->midi.channel, a->midi.number)].push_back(raw);
        if (!a->osc.empty()) next->osc[a->osc].push_back(raw);
        next->byParam[a->paramId] = raw;
        next->entries.push_back(std::move(e));
    }
    std::atomic_store(&snapshot_, std::shared_ptr<const Snapshot>(std::move(next)));
    revision_.fetch_add(1);
}

json ControlMappings::conflicts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto effective = effectiveLocked();
    std::vector<const ControlAssignment*> sys, other;
    for (const auto* a : effective) (a->scope == MapScope::System ? sys : other).push_back(a);
    json out = json::array();
    std::set<std::string> seen;
    auto describe = [](const ControlAssignment* a) {
        json j = {{"paramId", a->paramId}, {"scope", scopeName(a->scope)}};
        if (a->scope == MapScope::Module) j["module"] = a->module;
        if (a->scope == MapScope::Sound) j["layer"] = a->layer;
        return j;
    };
    for (const auto* o : other) {
        // MIDI sources
        if (o->midi.valid()) {
            json systemSide = json::array(), otherSide = json::array();
            for (const auto* s : sys) if (s->midi.overlaps(o->midi)) systemSide.push_back(describe(s));
            const std::string key = "midi:" + o->midi.label();
            if (!systemSide.empty() && !seen.count(key)) {
                for (const auto* p : other) if (p->midi.overlaps(o->midi)) otherSide.push_back(describe(p));
                out.push_back({{"type", "midi"}, {"source", o->midi.label()}, {"system", systemSide}, {"others", otherSide}});
                seen.insert(key);
            }
        }
        if (!o->osc.empty()) {
            json systemSide = json::array(), otherSide = json::array();
            for (const auto* s : sys) if (s->osc == o->osc) systemSide.push_back(describe(s));
            const std::string key = "osc:" + o->osc;
            if (!systemSide.empty() && !seen.count(key)) {
                for (const auto* p : other) if (p->osc == o->osc) otherSide.push_back(describe(p));
                out.push_back({{"type", "osc"}, {"source", "OSC " + o->osc}, {"system", systemSide}, {"others", otherSide}});
                seen.insert(key);
            }
        }
    }
    return out;
}

json ControlMappings::describe() const {
    json list = json::array();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto effective = effectiveLocked();
        auto isEffective = [&](const ControlAssignment& a) {
            return std::find(effective.begin(), effective.end(), &a) != effective.end();
        };
        auto add = [&](const ScopeMap& map) {
            for (const auto& [id, a] : map) {
                json j = a.toJson();
                j["effective"] = isEffective(a);
                if (a.midi.valid()) j["midiLabel"] = a.midi.label();
                list.push_back(j);
            }
        };
        add(system_); for (const auto& m : sound_) add(m); for (const auto& m : module_) add(m); add(program_);
    }
    return {{"assignments", list}, {"conflicts", conflicts()}, {"revision", revision_.load()}};
}

void ControlMappings::syncValues(const json& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto snap = std::atomic_load(&snapshot_);
    values_.clear();
    for (const auto& e : snap->entries) {
        if (auto v = readParamValue(params, e->a.paramId)) {
            values_[e->a.paramId] = *v;
            e->current.store(*v); e->known.store(true);
        }
        e->picked.store(false);   // after a load, knobs must pick the new values up again
        e->lastRaw.store(-1);
    }
}

void ControlMappings::noteWritten(const std::string& paramId, float value) {
    const auto snap = std::atomic_load(&snapshot_);
    const auto it = snap->byParam.find(paramId);
    if (it == snap->byParam.end()) return;
    const Entry& e = *it->second;
    const float tolerance = 1e-4f * std::max(1.f, std::abs(e.a.max - e.a.min));
    if (!e.known.load() || std::abs(e.current.load() - value) > tolerance) e.picked.store(false);
    e.current.store(value); e.known.store(true);
}

void ControlMappings::write(const Entry& e, float value) const {
    e.current.store(value); e.known.store(true);
    registry_.setParamValue(e.a.paramId, value, false);
    if (writeListener_) writeListener_(e.a.paramId);
}

void ControlMappings::apply(const Entry& e, int value) const {
    const auto& a = e.a;
    const float lo = a.invert ? a.max : a.min, hi = a.invert ? a.min : a.max;
    const int previous = e.lastRaw.exchange(value);
    switch (a.button) {
        case ButtonMode::Toggle:
            if (value >= 64 && previous < 64) {
                const float mid = .5f * (a.min + a.max);
                write(e, e.known.load() && e.current.load() > mid ? a.min : a.max);
            }
            return;
        case ButtonMode::Momentary:
            write(e, value >= 64 ? hi : lo);
            return;
        case ButtonMode::Continuous: break;
    }
    if (a.takeover == Takeover::Relative) {
        if (value == 64 || value == 0) return;
        const int delta = value < 64 ? value : value - 128;   // two's complement encoders
        const float step = (a.max - a.min) / 128.f * (a.invert ? -1.f : 1.f);
        const float base = e.known.load() ? e.current.load() : a.min;
        write(e, std::clamp(base + float(delta) * step, std::min(a.min, a.max), std::max(a.min, a.max)));
        return;
    }
    const float target = lo + (hi - lo) * float(value) / 127.f;
    if (a.takeover == Takeover::Jump || !e.known.load() || e.picked.load()) {
        e.picked.store(true);
        write(e, target);
        return;
    }
    // Pickup: wait until the hardware control reaches (or crosses) the current value.
    const float cur = e.current.load();
    const float tolerance = .02f * std::abs(hi - lo);
    bool crossed = false;
    if (previous >= 0) {
        const float before = lo + (hi - lo) * float(previous) / 127.f;
        crossed = (before - cur) * (target - cur) <= 0.f;
    }
    if (std::abs(target - cur) <= tolerance || crossed) {
        e.picked.store(true);
        write(e, target);
    }
}

bool ControlMappings::handleMidi(MidiKind kind, int channel, int number, int value) {
    if (number < 0 || number > 127 || channel < 1 || channel > 16) return false;
    {
        std::lock_guard<std::mutex> lock(learnMutex_);
        lastInput_ = {{"type", "midi"}, {"kind", kindName(kind)}, {"channel", channel}, {"number", number}, {"value", value}};
        const bool isRelease = kind == MidiKind::Note && value == 0;
        if (learnActive_ && !isRelease && (learnAllowReserved_ || !reservedForLearn(kind, number))) {
            learnActive_ = false;
            learnCaptured_ = {{"type", "midi"}, {"kind", kindName(kind)}, {"channel", channel}, {"number", number},
                              {"label", MidiSource{kind, channel, number}.label()}};
            ++learnRevision_;
        }
    }
    const auto snap = std::atomic_load(&snapshot_);
    bool consumed = false;
    for (const int ch : {channel, 0}) {
        const auto it = snap->midi.find(midiKey(kind, ch, number));
        if (it == snap->midi.end()) continue;
        for (const Entry* e : it->second) apply(*e, value);
        consumed = true;
    }
    return consumed;
}

bool ControlMappings::handleOsc(const std::string& address, float value) {
    {
        std::lock_guard<std::mutex> lock(learnMutex_);
        lastInput_ = {{"type", "osc"}, {"address", address}, {"value", value}};
        if (learnActive_ && validOscAddress(address)) {
            learnActive_ = false;
            learnCaptured_ = {{"type", "osc"}, {"address", address}, {"label", "OSC " + address}};
            ++learnRevision_;
        }
    }
    const auto snap = std::atomic_load(&snapshot_);
    const auto it = snap->osc.find(address);
    if (it == snap->osc.end() || !std::isfinite(value)) return false;
    const int raw = int(std::lround(std::clamp(value, 0.f, 1.f) * 127.f));
    for (const Entry* e : it->second) {
        // OSC is high resolution: continuous controls use the exact value, not 7 bits.
        if (e->a.button == ButtonMode::Continuous && e->a.takeover != Takeover::Relative) {
            const float lo = e->a.invert ? e->a.max : e->a.min, hi = e->a.invert ? e->a.min : e->a.max;
            e->picked.store(true);
            e->lastRaw.store(raw);
            write(*e, lo + (hi - lo) * std::clamp(value, 0.f, 1.f));
        } else {
            apply(*e, raw);
        }
    }
    return true;
}

void ControlMappings::armLearn(bool allowReserved) {
    std::lock_guard<std::mutex> lock(learnMutex_);
    learnActive_ = true; learnAllowReserved_ = allowReserved; learnCaptured_ = nullptr; ++learnRevision_;
}
void ControlMappings::cancelLearn() {
    std::lock_guard<std::mutex> lock(learnMutex_);
    learnActive_ = false; ++learnRevision_;
}
json ControlMappings::learnState() const {
    std::lock_guard<std::mutex> lock(learnMutex_);
    return {{"active", learnActive_}, {"captured", learnCaptured_}, {"revision", learnRevision_}};
}
json ControlMappings::lastInput() const {
    std::lock_guard<std::mutex> lock(learnMutex_);
    return lastInput_;
}

json ControlMappings::migrateLegacyMidi(const json& bindings, const ParameterRegistry& registry, MapScope scope, int module) {
    json out = json::array();
    std::set<std::string> taken;
    if (!bindings.is_array()) return out;
    for (const auto& b : bindings) {
        if (!b.is_object()) continue;
        const auto id = b.value("paramId", std::string{});
        if (taken.count(id)) continue;   // v2 allowed several CCs per parameter: the first one wins
        json j = {{"paramId", id}, {"midi", {{"type", "cc"}, {"channel", b.value("channel", 0)}, {"number", b.value("cc", -1)}}}};
        if (b.contains("min")) j["min"] = b["min"];
        if (b.contains("max")) j["max"] = b["max"];
        if (auto a = ControlAssignment::fromJson(j, registry, scope, module)) { out.push_back(a->toJson()); taken.insert(id); }
    }
    return out;
}

json ControlMappings::migrateLegacyOsc(const json& routes, const ParameterRegistry& registry, MapScope scope, int module) {
    json out = json::array();
    if (!routes.is_array()) return out;
    for (const auto& r : routes) {
        if (!r.is_object()) continue;
        json j = {{"paramId", r.value("paramId", std::string{})}, {"osc", r.value("address", std::string{})}};
        if (auto a = ControlAssignment::fromJson(j, registry, scope, module)) out.push_back(a->toJson());
    }
    return out;
}

} // namespace NordDSP
