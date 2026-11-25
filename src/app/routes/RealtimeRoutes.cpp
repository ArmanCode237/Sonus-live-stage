// Real-time control channel: WebSocket /ws (httplib 0.65 built-in server).
// The pre-routing guard (Host/Origin) also runs on the upgrade handshake.
//
// UI → engine messages (JSON):
//   {"op":"set","id":"L1_cut","v":1200,"seq":7}   parameter write (ParameterRegistry)
//   {"op":"note","key":60,"vel":100,"on":true}     virtual keyboard
//   {"op":"resync"}                                 client detected a patch gap
// Every op carrying "seq" is answered with {"t":"ack","seq":7,"ok":true}.
// Engine → UI frames come from StateHub (snapshot / patch / state).
#include "app/routes/RouteCommon.h"
#include "app/StateHub.h"

using namespace SonusDSP;

void StageApp::registerRealtimeRoutes() {
    svr.WebSocket("/ws", [this](const httplib::Request&, httplib::ws::WebSocket& ws) {
        auto client = hub->attach();
        if (!client) { ws.close(httplib::ws::CloseStatus::Normal, "too many clients"); return; }
        // Short read timeout: the same thread alternates between sending queued
        // frames and reading commands, so only this thread ever writes to `ws`.
        ws.set_read_timeout(std::chrono::milliseconds(4));
        std::string message;
        // svr.stop() waits for every handler: leave as soon as the server stops, even if
        // the peer keeps the socket open (WKWebView does after its window closes).
        while (ws.is_open() && !client->closed() && !httpStopping_.load()) {
            bool sendFailed = false;
            for (const auto& frame : client->take(std::chrono::milliseconds(4)))
                if (!ws.send(frame)) { sendFailed = true; break; }
            if (sendFailed) break;
            const auto result = ws.read(message);
            if (result == httplib::ws::Text) {
                const auto reply = handleRealtimeCommand(message, client);
                if (!reply.empty() && !ws.send(reply)) break;
            } else if (result == httplib::ws::Fail) {
                break;
            }
        }
        hub->detach(client);
    });
}

std::string StageApp::handleRealtimeCommand(const std::string& text, const std::shared_ptr<StateHub::Client>& client) {
    const json m = json::parse(text, nullptr, false);
    if (!m.is_object()) return {};
    const std::string op = m.value("op", std::string{});
    bool ok = false;
    if (op == "set") {
        // Same path as POST /api/param: registry setter → engine atomics.
        const std::string id = m.value("id", std::string{});
        const json& v = m.contains("v") ? m["v"] : json();
        if (!id.empty() && v.is_number()) {
            ok = engine.applyParamById(id, v.get<float>(), false);
            if (ok) { markParamEdited(id); paramRevCounter.fetch_add(1); }
        }
    } else if (op == "note") {
        const int key = std::clamp(m.value("key", 60), 0, 127);
        if (m.value("on", true)) engine.noteOn(key, std::clamp(m.value("vel", 100), 1, 127));
        else engine.noteOff(key);
        ok = true;
    } else if (op == "resync") {
        hub->resync(client);
        ok = true;
    }
    if (!m.contains("seq")) return {};
    return json{{"t", "ack"}, {"seq", m["seq"]}, {"ok", ok}}.dump();
}
