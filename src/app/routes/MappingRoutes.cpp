// MIDI / OSC mappings (v31): three scopes, learn by capture, conflicts with SYSTEM.
//   GET  /api/mappings         every assignment + conflicts + learn state + OSC status
//   GET  /api/mappings/learn   learn state only (polled while the UI waits)
//   POST /api/mappings {op}
//     learn  {allowReserved?}            capture the next MIDI message / OSC address
//     cancel                              stop learning
//     assign {paramId, scope, midi?, osc?, min?, max?, invert?, takeover?, button?}
//            merged into the parameter's assignment in that scope; "midi": null / "osc": null clear a part
//     remove {paramId, scope, midi?, osc?}  both parts when neither flag is given
//     oscPort {port}
//     inject {midi:{type,channel,number,value}} | {osc:{address,value}}   virtual input (tests, on-screen pads)
#include "app/routes/RouteCommon.h"

using namespace SonusDSP;

namespace {
json mappingState(ControlMappings& m, MidiOscMapper& osc) {
    json state = m.describe();
    state["ok"] = true;
    state["learn"] = m.learnState();
    state["lastInput"] = m.lastInput();
    state["osc"] = {{"port", osc.getOscPort()}, {"running", osc.isOscRunning()}, {"lastAddress", osc.getLastOscAddress()}};
    return state;
}
}

void StageApp::registerMappingRoutes() {
    svr.Get("/api/mappings", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        res.set_content(mappingState(engine.getMappings(), oscMapper).dump(), "application/json");
    });

    svr.Get("/api/mappings/learn", [this](const httplib::Request&, httplib::Response& res) {
        auto& m = engine.getMappings();
        res.set_header("Cache-Control", "no-store");
        res.set_content(json{{"ok", true}, {"learn", m.learnState()}, {"lastInput", m.lastInput()}}.dump(), "application/json");
    });

    svr.Post("/api/mappings", [this](const httplib::Request& req, httplib::Response& res) {
        const json b = json::parse(req.body, nullptr, false);
        auto& m = engine.getMappings();
        auto fail = [&res](int status, const std::string& error) {
            res.status = status;
            res.set_content(json{{"ok", false}, {"error", error}}.dump(), "application/json");
        };
        if (!b.is_object()) return fail(400, "Petición inválida");
        const std::string op = b.value("op", std::string{});
        // Scope changes are persisted where they belong: SYSTEM to disk now, MODULE marks
        // the module preset as modified (saved with GUARDAR), PROGRAM is saved with STORE.
        auto changed = [&](MapScope scope, const std::string& paramId) {
            if (scope == MapScope::System) persistControlMaps();
            else if (scope == MapScope::Module) {
                const int module = parameterModule(paramId);
                if (module >= 0) moduleDirty[size_t(module)].store(true);
            } else if (scope == MapScope::Sound) {
                const int layer = soundLayerOfParam(paramId);
                if (layer >= 0) soundDirty[size_t(layer)].store(true);
                moduleDirty[0].store(true);   // the performance holds a copy of its Sounds
            }
            syncMappingValues();
        };

        if (op == "learn") { m.armLearn(b.value("allowReserved", false)); }
        else if (op == "cancel") { m.cancelLearn(); }
        else if (op == "assign" || op == "remove") {
            const std::string paramId = b.value("paramId", std::string{});
            const auto scope = parseScope(b.value("scope", std::string{}));
            if (!scope) return fail(400, "Ámbito inválido (system, sound, module, program)");
            if (op == "remove") {
                const bool midiOnly = b.value("midi", false), oscOnly = b.value("osc", false);
                const bool both = !midiOnly && !oscOnly;
                if (!m.remove(paramId, *scope, both || midiOnly, both || oscOnly)) return fail(404, "No hay mapeo en ese ámbito");
            } else {
                json merged = json{{"paramId", paramId}};
                if (auto existing = m.get(paramId, *scope)) merged = existing->toJson();
                for (const char* key : {"midi", "osc", "min", "max", "invert", "takeover", "button"})
                    if (b.contains(key)) merged[key] = b[key];
                std::string error;
                const int module = *scope == MapScope::Module ? parameterModule(paramId)
                                 : *scope == MapScope::Sound ? soundLayerOfParam(paramId) : -1;
                const auto a = ControlAssignment::fromJson(merged, engine.getRegistry(), *scope, module, &error);
                if (!a) return fail(400, error);
                if (!m.assign(*a, &error)) return fail(400, error);
            }
            changed(*scope, paramId);
        }
        else if (op == "oscPort") {
            const int port = b.value("port", 0);
            if (port < 1024 || port > 65535) return fail(400, "Puerto OSC fuera de rango (1024-65535)");
            oscMapper.startOscServer(port);
            persistControlMaps();
        }
        else if (op == "inject") {
            if (b.contains("midi") && b["midi"].is_object()) {
                const auto& v = b["midi"];
                const auto kind = v.value("type", std::string("cc")) == "note" ? MidiKind::Note : MidiKind::CC;
                const int channel = std::clamp(v.value("channel", 1), 1, 16), number = v.value("number", -1);
                const int value = std::clamp(v.value("value", 0), 0, 127);
                if (number < 0 || number > 127) return fail(400, "Mensaje MIDI inválido");
                // Same order as the MIDI callback: the wheel always reaches the modules.
                if (kind == MidiKind::CC && number == 1) engine.setModWheel(value / 127.f);
                m.handleMidi(kind, channel, number, value, std::clamp(v.value("src", 0), 0, 15));   // v33: simulated keyboard
            } else if (b.contains("osc") && b["osc"].is_object()) {
                const auto& v = b["osc"];
                m.handleOsc(v.value("address", std::string{}), v.value("value", 0.f));
            } else return fail(400, "Falta midi u osc");
            paramRevCounter.fetch_add(1);
        }
        else return fail(400, "Operación desconocida");
        res.set_content(mappingState(m, oscMapper).dump(), "application/json");
    });
}
