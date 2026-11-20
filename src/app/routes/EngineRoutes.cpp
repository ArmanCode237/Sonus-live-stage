// Generated from the former monolithic main.cpp: route bodies are unchanged.
#include "app/routes/RouteCommon.h"

using namespace SonusDSP;

void StageApp::registerEngineRoutes() {
    // New four-rack architecture: local PIANO/SYNTH/ORGAN processing then
    // the module-level GLOBAL rack. Strict input validation, no file I/O here.
    svr.Get("/api/studio-racks", [this](const httplib::Request&, httplib::Response& res){
        const json p=serializeParams(engine.getCurrentParams());
        res.set_header("Cache-Control","no-store");
        res.set_content(json{{"ok",true},{"mode",p["studioRoutingEnabled"]},
                             {"racks",p["studioRacks"]},{"version",4}}.dump(),"application/json");
    });

    svr.Post("/api/studio-racks", [this](const httplib::Request& req, httplib::Response& res){
        const json b=json::parse(req.body,nullptr,false);
        if(!b.is_object()){
            res.status=400;res.set_content(R"({"ok":false,"error":"JSON invalido"})","application/json");return;
        }
        const std::string op=b.value("op",std::string());
        const int rack=b.value("rack",-1),slot=b.value("slot",-1);
        bool ok=false;
        if(op=="mode"&&b.contains("value")&&b["value"].is_boolean()){
            engine.setStudioRoutingEnabled(b["value"].get<bool>());ok=true;
        }else if(rack>=0&&rack<4){
            if(op=="swap" && b.contains("target") && b["target"].is_number_integer())
                ok=engine.swapStudioSlots(rack,slot,b["target"].get<int>());
            else if(op=="slot" && b.contains("field")&&b["field"].is_number_integer() && b.contains("value")&&b["value"].is_number())
                ok=engine.setStudioSlot(rack,slot,b["field"].get<int>(),b["value"].get<float>());
            else if(op=="send"&&b.contains("source")&&b["source"].is_number_integer()&&b.contains("value")&&b["value"].is_number())
                ok=engine.setStudioSend(rack,b["source"].get<int>(),slot,b["value"].get<float>());
            else if(op=="eq"&&b.contains("band")&&b["band"].is_number_integer()&&b.contains("value")&&b["value"].is_number())
                ok=engine.setStudioEq(rack,b["band"].get<int>(),b["value"].get<float>());
            else if(op=="eqParam"&&b.contains("field")&&b["field"].is_number_integer()&&b.contains("value")&&b["value"].is_number())
                ok=engine.setStudioEqParam(rack,b["field"].get<int>(),b["value"].get<float>());
            else if(op=="eqEnabled"&&b.contains("value")&&b["value"].is_boolean())
                ok=engine.setStudioEqEnabled(rack,b["value"].get<bool>());
        }
        if(!ok){res.status=400;res.set_content(R"({"ok":false,"error":"Parametro fuera de rango"})","application/json");return;}
        paramRevCounter.fetch_add(1);
        res.set_content(json{{"ok",true}}.dump(),"application/json");
    });

    svr.Post("/api/param", [this](const httplib::Request& req, httplib::Response& res) {
        auto b = json::parse(req.body, nullptr, false);
        std::string id = b.value("id", "");
        float val = b.value("value", 0.0f);

        bool applied = engine.applyParamById(id, val, false);
        if (applied) markParamEdited(id);
        paramRevCounter.fetch_add(1);

        if (id == "synthPresetId") {
            res.set_content(json({
                {"ok", applied},
                {"params", serializeParams(engine.getCurrentParams())}
            }).dump(), "application/json");
            return;
        }
        res.set_content(json({{"ok", applied}}).dump(), "application/json");
    });

    svr.Post("/api/note", [this](const httplib::Request& req, httplib::Response& res) {
        auto b = json::parse(req.body, nullptr, false);
        int key = b.value("key", 60);
        int vel = b.value("vel", 100);
        bool on = b.value("on", true);
        // v33: src (0 internal, 1-15 MIDI ports) and ch (1-16) simulate a keyboard for routing tests.
        const int src = std::clamp(b.value("src", 0), 0, 15), ch = std::clamp(b.value("ch", 0), 0, 16);
        if (on) engine.noteOn(key, vel, src, ch);
        else engine.noteOff(key, src, ch);
        res.set_content("{\"ok\":true}", "application/json");
    });

    svr.Post("/api/testtone", [this](const httplib::Request&, httplib::Response& res) {
        engine.triggerTestTone();
        res.set_content("{\"ok\":true}", "application/json");
    });
}
