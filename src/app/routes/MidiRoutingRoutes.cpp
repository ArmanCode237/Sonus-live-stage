// v33 (M2) MIDI routing, Reaper style: each (keyboard, channel) plays chosen destinations.
//   GET  /api/midi/routing  -> {enabled, rules:[{source, channel, dest}], sources:[...], destinations:[...]}
//   POST /api/midi/routing  {enabled?, rules?}
// source: "*" (any), "internal" (on-screen keyboard / OSC / the DAW track in the plug-in) or a
// MIDI port name; channel: 0 = omni, 1..16; dest: bits 0-4 piano L1-L5, 5-8 synth A-D, 9 organ.
// Disabled (default) = everything plays everything, exactly as before v33.
#include "app/routes/RouteCommon.h"
#include "app/MidiInput.h"

using namespace SonusDSP;

namespace {
constexpr int kSources = StagePianoEngine::kRouteSources, kChannels = StagePianoEngine::kRouteChannels;
constexpr uint16_t kAll = StagePianoEngine::kRouteAll;

json sanitizeRouting(const json& in) {
    json out = {{"enabled", false}, {"pedalFollows", true}, {"rules", json::array()}};
    if (!in.is_object()) return out;
    out["enabled"] = in.value("enabled", false);
    out["pedalFollows"] = in.value("pedalFollows", true);   // M3: each keyboard's pedal holds only its layers
    if (in.contains("rules") && in["rules"].is_array()) {
        for (const auto& r : in["rules"]) {
            if (!r.is_object() || out["rules"].size() >= 64) continue;
            std::string source = r.value("source", std::string("*"));
            if (source.empty()) source = "*";
            if (source.size() > 128) source.resize(128);
            const int channel = std::clamp(r.value("channel", 0), 0, 16);
            const int dest = r.value("dest", int(kAll)) & int(kAll);
            out["rules"].push_back({{"source", source}, {"channel", channel}, {"dest", dest}});
        }
    }
    return out;
}
}

json StageApp::defaultMidiRouting() {
    return {{"enabled", false}, {"pedalFollows", true}, {"rules", json::array({{{"source", "*"}, {"channel", 0}, {"dest", int(kAll)}}})}};
}

void StageApp::compileMidiRoutes() {
    json routing;
    { std::lock_guard<std::mutex> lock(routingMutex_); routing = midiRouting_; }
    std::array<uint16_t, kSources * kChannels> table{};
    engine.setSustainFollowsRouting(routing.value("pedalFollows", true));
    if (!routing.value("enabled", false)) {
        table.fill(kAll);
    } else {
        std::vector<std::pair<std::string, int>> ids;   // port name -> source id
        if (hw) for (const auto& s : hw->midiSources()) if (s.id > 0) ids.emplace_back(s.name, s.id);
        for (const auto& r : routing["rules"]) {
            const std::string source = r.value("source", std::string("*"));
            const int channel = r.value("channel", 0);
            const uint16_t dest = uint16_t(r.value("dest", 0) & kAll);
            std::vector<int> sources;
            if (source == "*") for (int s = 0; s < kSources; ++s) sources.push_back(s);
            else if (source == "internal") sources.push_back(0);
            else for (const auto& [name, id] : ids) if (name == source) sources.push_back(id);
            for (const int s : sources)
                for (int c = 0; c < kChannels; ++c)
                    if (channel == 0 || c == channel) table[size_t(s * kChannels + c)] |= dest;
        }
    }
    for (int s = 0; s < kSources; ++s)
        for (int c = 0; c < kChannels; ++c) engine.setMidiRoute(s, c, table[size_t(s * kChannels + c)]);
    // v33 (M4): device-specific mappings compare against these names.
    std::array<std::string, 16> names{};
    names[0] = "internal";
    if (hw) for (const auto& s : hw->midiSources()) if (s.id > 0 && s.id < 16) names[size_t(s.id)] = s.name;
    engine.getMappings().setSourceNames(names);
}

json StageApp::midiRoutingJson() {
    json out;
    { std::lock_guard<std::mutex> lock(routingMutex_); out = midiRouting_; }
    json sources = json::array();
    sources.push_back({{"id", 0}, {"name", "internal"}, {"connected", true}, {"enabled", true},
                       {"label", options_.plugin ? "DAW (pista)" : "Teclado en pantalla / OSC"}});
    if (hw) for (const auto& s : hw->midiSources())
        sources.push_back({{"id", s.id}, {"name", s.name}, {"connected", s.connected}, {"enabled", s.enabled}, {"label", s.name}});
    out["sources"] = sources;
    out["host"] = hw ? hw->hostKind() : "app";
    out["destinations"] = json::array({"L1", "L2", "L3", "L4", "L5", "A", "B", "C", "D", "ORGAN"});
    return out;
}

void StageApp::registerMidiRoutingRoutes() {
    // Raw MIDI through the same interpreter as the keyboards (tests / virtual keyboards):
    // {bytes:[status, data1, data2], src}
    svr.Post("/api/midi/inject", [this](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        if (!body.is_object() || !body.contains("bytes") || !body["bytes"].is_array() || body["bytes"].size() > 3) {
            res.status = 400; res.set_content(R"({"ok":false})", "application/json"); return;
        }
        unsigned char bytes[3] = {0, 0, 0};
        size_t n = 0;
        for (const auto& b : body["bytes"]) bytes[n++] = (unsigned char)std::clamp(b.is_number_integer() ? b.get<int>() : 0, 0, 255);
        dispatchMidiMessage(engine, bytes, n, std::clamp(body.value("src", 0), 0, 15));
        res.set_content(R"({"ok":true})", "application/json");
    });
    svr.Get("/api/midi/routing", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(midiRoutingJson().dump(), "application/json");
    });
    svr.Post("/api/midi/routing", [this](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        if (!body.is_object()) { res.status = 400; res.set_content(R"({"ok":false,"error":"JSON inválido"})", "application/json"); return; }
        {
            std::lock_guard<std::mutex> lock(routingMutex_);
            json next = midiRouting_;
            if (body.contains("enabled") && body["enabled"].is_boolean()) next["enabled"] = body["enabled"];
            if (body.contains("rules") && body["rules"].is_array()) next["rules"] = body["rules"];
            if (body.contains("pedalFollows") && body["pedalFollows"].is_boolean()) next["pedalFollows"] = body["pedalFollows"];
            midiRouting_ = sanitizeRouting(next);
        }
        compileMidiRoutes();
        saveSystemSettings();
        auto out = midiRoutingJson();
        out["ok"] = true;
        res.set_content(out.dump(), "application/json");
    });
}
