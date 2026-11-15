// Generated from the former monolithic main.cpp: route bodies are unchanged.
#include "app/routes/RouteCommon.h"

using namespace NordDSP;

void StageApp::registerOrganRoutes() {
    // Factory catalogue is readable on disk and never overwritten by STORE.
    svr.Get("/api/organ/factory", [this](const httplib::Request&,httplib::Response& res){
        json items=json::array();
        for(int i=1;i<=8;i++){
            std::ostringstream label;label<<std::setw(3)<<std::setfill('0')<<i<<".json";
            std::ifstream f(fs::path(findUiDir()).parent_path()/"presets/organ/factory"/label.str());
            if(f)try{items.push_back(json::parse(f));}catch(...){}
        }
        res.set_content(items.dump(),"application/json");
    });

    svr.Post("/api/organ/factory/load",[this](const httplib::Request& req,httplib::Response& res){
        const auto b=json::parse(req.body,nullptr,false);
        const int id=b.is_object()?b.value("id",-1):-1;
        if(id<0||id>=8){res.status=400;res.set_content(R"({"ok":false})","application/json");return;}
        std::ostringstream label;label<<std::setw(3)<<std::setfill('0')<<id+1<<".json";
        std::ifstream f(fs::path(findUiDir()).parent_path()/"presets/organ/factory"/label.str());
        if(!f){res.status=404;res.set_content(R"({"ok":false})","application/json");return;}
        try{const auto patch=json::parse(f);
            // Factory organ presets go through the same module path (mappings, dirty flag).
            json params=patch.at("params");
            restoreModulePatch(2,params);
            paramRevCounter.fetch_add(1);res.set_content(json{{"ok",true},{"params",serializeParams(engine.getCurrentParams())}}.dump(),"application/json");}
        catch(...){res.status=422;res.set_content(R"({"ok":false})","application/json");}
    });

    svr.Get("/api/organ/presets", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        res.set_content(loadPresetsFile(organPresetsPath).dump(), "application/json");
    });

    svr.Post("/api/organ/presets/save", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string name = body.value("name", "Untitled");
        json presets = loadPresetsFile(organPresetsPath);
        json newPreset = {
            {"name", name},
            {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()},
            {"params", modulePatch(2)}
        };
        bool replaced = false;
        for (auto& p : presets) {
            if (p.value("name", "") == name) { p = newPreset; replaced = true; break; }
        }
        if (!replaced) presets.push_back(newPreset);
        savePresetsFile(organPresetsPath, presets);
        res.set_content(json({{"ok", true}, {"presets", presets}}).dump(), "application/json");
    });

    svr.Post("/api/organ/presets/load", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string name = body.value("name", "");
        json presets = loadPresetsFile(organPresetsPath);
        for (const auto& p : presets) {
            if (p.value("name", "") == name) {
                const json& pData = p.contains("params") ? p["params"] : p;
                restoreModulePatch(2,pData);
                selectedSoundRefs["organ"]=nullptr;
                paramRevCounter.fetch_add(1);
                res.set_content(json({
                    {"ok", true},
                    {"preset", p},
                    {"params", serializeParams(engine.getCurrentParams())}
                }).dump(), "application/json");
                return;
            }
        }
        res.set_content("{\"ok\":false,\"error\":\"Preset no encontrado\"}", "application/json");
    });

    svr.Post("/api/organ/presets/delete", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string name = body.value("name", "");
        json presets = loadPresetsFile(organPresetsPath);
        presets.erase(std::remove_if(presets.begin(), presets.end(),
            [&](const json& p) { return p.value("name", "") == name; }), presets.end());
        savePresetsFile(organPresetsPath, presets);
        res.set_content(json({{"ok", true}, {"presets", presets}}).dump(), "application/json");
    });

    svr.Post("/api/organ/loadsample", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        std::string path = body.value("path", "");
        bool ok = engine.loadOrganSample(path);
        res.set_content(json({{"ok", ok}, {"path", path}}).dump(), "application/json");
    });
}
