// Generated from the former monolithic main.cpp: route bodies are unchanged.
#include "app/routes/RouteCommon.h"

using namespace SonusDSP;

json StageApp::readOverrideFile(){
    json metadata={{"overrides",json::object()},{"favorites",json::array()},{"hidden",json::array()}};
    std::ifstream in(factoryOverridesPath);
    if(in)try{auto parsed=json::parse(in);if(parsed.is_object()){
        for(auto key:{"overrides","favorites","hidden"})if(parsed.contains(key))metadata[key]=parsed[key];
    }}catch(...){}
    if(!metadata["overrides"].is_object())metadata["overrides"]=json::object();
    for(auto key:{"favorites","hidden"})if(!metadata[key].is_array())metadata[key]=json::array();
    return metadata;
}

json StageApp::factoryState(){
    const auto p = engine.getCurrentParams();
    json layerTimbres = json::array();
    int enabledLayers = 0;
    for (const auto& layer : p.synthLayers) {
        if (layer.enabled) ++enabledLayers;
        layerTimbres.push_back({
            {"enabled", layer.enabled}, {"wave", layer.waveType},
            {"cutoff", layer.cutoffHz}, {"attack", layer.attackSec},
            {"volume", layer.volume}
        });
    }
    return json({
        {"ok", true}, {"build", factoryBuild},
        {"apiVersion", 5}, {"maxFactoryPresets", kFactoryPresetCount},
        {"presetId", p.synthPresetId}, {"synthEnabled", p.synthEnabled},
        {"synthMasterVol", p.synthMasterVol},
        {"enabledLayers", enabledLayers}, {"layerTimbres", layerTimbres},
        {"uiRoot", findUiDir()}
    });
}

void StageApp::registerSynthRoutes() {
    // A deleted factory preset is hidden non-destructively; bundled assets are read-only.
    svr.Get("/api/synth/factory/edits",[this](const httplib::Request&,httplib::Response& res){
        std::lock_guard<std::mutex> lock(dataMutex);
        res.set_content(readOverrideFile().dump(),"application/json");
    });

    svr.Post("/api/synth/factory/save",[this](const httplib::Request& req,httplib::Response& res){
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()||!b.contains("id")||!b["id"].is_number_integer()||b["id"].get<int>()<1||b["id"].get<int>()>kFactoryPresetCount){
            res.status=400;res.set_content(R"({"ok":false,"error":"ID de fábrica inválido"})","application/json");return;}
        std::lock_guard<std::mutex> lock(dataMutex);
        const int id=b["id"].get<int>();std::string name=b.value("name",std::string("Preset ")+std::to_string(id));
        if(!checkPresetName(name)){res.status=400;res.set_content(R"({"ok":false,"error":"Nombre vacío"})","application/json");return;}
        auto meta=readOverrideFile();
        meta["overrides"][std::to_string(id)]={{"name",name},{"params",modulePatch(1)}};
        if(!saveJsonAtomic(factoryOverridesPath,meta)){res.status=500;res.set_content(R"({"ok":false,"error":"No se pudo guardar el preset"})","application/json");return;}
        selectedSoundRefs["synthPreset"]=nullptr;
        res.set_content(json{{"ok",true},{"metadata",meta}}.dump(),"application/json");
    });

    svr.Post("/api/synth/factory/meta",[this](const httplib::Request& req,httplib::Response& res){
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()||!b.contains("id")||!b["id"].is_number_integer() ||
           b["id"].get<int>()<1||b["id"].get<int>()>kFactoryPresetCount){res.status=400;res.set_content(R"({"ok":false})","application/json");return;}
        const int id=b["id"].get<int>();const std::string op=b.value("op",std::string());
        if(op!="favorite"&&op!="delete"&&op!="restore"&&op!="reset"){
            res.status=400;res.set_content(R"({"ok":false})","application/json");return;}
        std::lock_guard<std::mutex> lock(dataMutex);
        auto meta=readOverrideFile();const std::string key=std::to_string(id);
        auto toggle=[&](const char* group,bool enable){
            auto& a=meta[group];json next=json::array();
            for(const auto& v:a)if(v!=id)next.push_back(v);
            if(enable)next.push_back(id);a=next;
        };
        if(op=="favorite")toggle("favorites",b.value("favorite",true));
        else if(op=="delete")toggle("hidden",true);
        else if(op=="restore")toggle("hidden",false);
        else {meta["overrides"].erase(key);toggle("hidden",false);}
        if(!saveJsonAtomic(factoryOverridesPath,meta)){res.status=500;res.set_content(R"({"ok":false})","application/json");return;}
        res.set_content(json{{"ok",true},{"metadata",meta}}.dump(),"application/json");
    });

    svr.Get("/api/synth/factory/status", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store, max-age=0");
        res.set_content(factoryState().dump(), "application/json");
    });

    // Factory synth patches are independent from the 8 stored performance programs.
    // One endpoint avoids a two-request race (power ON, then preset selection).
    svr.Post("/api/synth/factory/load", [this](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        if (!body.is_object() || !body.contains("id") || !body["id"].is_number_integer()) {
            res.status = 400;
            res.set_content(json({{"ok",false},{"error","Se requiere un id entero de 1 a "+std::to_string(kFactoryPresetCount)}}).dump(), "application/json");
            return;
        }
        const int id = body["id"].get<int>();
        if (id < 1 || id > kFactoryPresetCount) {
            res.status = 400;
            res.set_content(json({{"ok",false},{"error","Preset fuera de rango (1 a "+std::to_string(kFactoryPresetCount)+")"}}).dump(), "application/json");
            return;
        }
        std::lock_guard<std::mutex> factoryGuard(dataMutex);
        const bool changed = engine.applyParamById("synthPresetId", static_cast<float>(id), false);
        const bool powered = engine.applyParamById("synthEnabled", 1.0f, false);
        // Factory JSON is the editable on-disk source; the built-in algorithm
        // provides a deterministic fallback if a factory file is missing.
        if(changed){
            bool restoredFromDisk=false;
            std::ostringstream fileName;
            fileName<<std::setw(3)<<std::setfill('0')<<id<<".json";
            const fs::path factory=fs::path(findUiDir()).parent_path()/"presets/synth/factory"/fileName.str();
            if(fs::exists(factory))try{
                std::ifstream f(factory);const auto patch=json::parse(f);
                if(patch.is_object() && patch.contains("params") && patch["params"].is_object()){
                    // Same module-scoped path as user presets: also restores FX rack.
                    restoredFromDisk=restoreModulePatch(1,patch["params"]);
                }
            }catch(const std::exception& e){std::cerr<<"[FACTORY] JSON error: "<<e.what()<<"\n";}
            if(!restoredFromDisk){
                engine.setStudioRack(1,StudioRackConfig{});
                engine.getMappings().replaceScope(MapScope::Module,1,json::array());
                moduleDirty[1].store(false);
                syncMappingValues();
            }
            const auto meta=readOverrideFile();
            const auto key=std::to_string(id);
            if(meta["overrides"].contains(key)&&meta["overrides"][key].is_object()){
                restoreModulePatch(1,meta["overrides"][key].value("params",json::object()));
            }
        }
        if (!changed || !powered) {
            res.status = 500;
            res.set_content(R"({"ok":false,"error":"No fue posible activar el sintetizador o cargar el preset"})", "application/json");
            return;
        }
        // A newly selected factory patch replaces any unsaved synth Sound edits.
        selectedSoundRefs["synth"]=nullptr;
        selectedSoundRefs["synthPreset"]=nullptr;   // the displays show the factory name
        paramRevCounter.fetch_add(1);
        const auto params = engine.getCurrentParams();
        const bool active = (params.synthPresetId == id && params.synthEnabled);
        auto diagnostic = factoryState();
        diagnostic["ok"] = active;
        diagnostic["params"] = serializeParams(params);
        const auto meta=readOverrideFile();
        if(meta["overrides"].contains(std::to_string(id)))
            diagnostic["name"]=meta["overrides"][std::to_string(id)].value("name",std::string{});
        res.set_header("Cache-Control", "no-store, max-age=0");
        res.set_content(diagnostic.dump(), "application/json");
    });

    svr.Get("/api/synth/presets", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        res.set_content(loadPresetsFile(synthPresetsPath).dump(), "application/json");
    });

    svr.Post("/api/synth/presets/save", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        if(!body.is_object()){res.status=400;res.set_content(R"({"ok":false})","application/json");return;}
        std::string name=body.value("name",std::string{});
        if(!checkPresetName(name)){res.status=400;res.set_content(R"({"ok":false,"error":"Nombre vacío"})","application/json");return;}
        std::string oldName=body.value("originalName",name);
        const bool createNew=body.value("mode",std::string{})=="new";
        json presets = loadPresetsFile(synthPresetsPath);
        const bool collision=std::any_of(presets.begin(),presets.end(),[&](const json& item){
            return item.is_object() && item.value("name",std::string{})==name;
        });
        if(collision&&(createNew||oldName!=name)){
            res.status=409;res.set_content(R"({"ok":false,"error":"Ya existe otro preset con ese nombre"})","application/json");return;
        }
        bool replaced=false;
        if(!createNew)for(auto& item:presets)if(item.is_object()&&item.value("name",std::string{})==oldName){
            const bool favorite=item.value("favorite",false);replaced=true;
            item={{"name",name},{"favorite",favorite},{"params",modulePatch(1)}};break;
        }
        if(!replaced)presets.push_back({{"name",name},{"favorite",false},{"params",modulePatch(1)}});
        const bool ok=saveJsonAtomic(synthPresetsPath,presets);if(!ok)res.status=500;
        else selectedSoundRefs["synthPreset"]=json{{"name",name},{"kind","USER"}};   // the sound playing now is this preset
        res.set_content(json{{"ok",ok},{"presets",presets}}.dump(),"application/json");
    });

    svr.Post("/api/synth/presets/load", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string name = body.value("name", "");
        json presets = loadPresetsFile(synthPresetsPath);
        for (const auto& p : presets) {
            if (p.value("name", "") == name) {
                const json& pData = p.contains("params") ? p["params"] : p;
                restoreModulePatch(1,pData);
                selectedSoundRefs["synth"]=nullptr;
                selectedSoundRefs["synthPreset"]=json{{"name",name},{"kind","USER"}};
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

    svr.Post("/api/synth/presets/delete", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto body = json::parse(req.body, nullptr, false);
        std::string name = body.value("name", "");
        json presets = loadPresetsFile(synthPresetsPath);
        presets.erase(std::remove_if(presets.begin(), presets.end(),
            [&](const json& p) { return p.value("name", "") == name; }), presets.end());
        savePresetsFile(synthPresetsPath, presets);
        if(presetRefName(selectedSoundRefs,"synthPreset")==name)selectedSoundRefs["synthPreset"]=nullptr;
        res.set_content(json({{"ok", true}, {"presets", presets}}).dump(), "application/json");
    });

    svr.Post("/api/synth/presets/favorite",[this](const httplib::Request& req,httplib::Response& res){
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()){res.status=400;res.set_content(R"({"ok":false})","application/json");return;}
        std::lock_guard<std::mutex> lock(dataMutex);
        auto presets=loadPresetsFile(synthPresetsPath);bool found=false;
        for(auto& item:presets)if(item.value("name",std::string{})==b.value("name",std::string{})){
            item["favorite"]=b.value("favorite",true);found=true;break;
        }
        if(!found){res.status=404;res.set_content(R"({"ok":false})","application/json");return;}
        const bool ok=saveJsonAtomic(synthPresetsPath,presets);if(!ok)res.status=500;
        res.set_content(json{{"ok",ok},{"presets",presets}}.dump(),"application/json");
    });

    svr.Get("/api/synth/wavetables", [this](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        const auto tables=engine.getWavetableSnapshot();
        if(tables)for (const auto& wt : *tables) {
            arr.push_back({{"name", wt.name}, {"custom", wt.custom}, {"size", SYNTH_WAVETABLE_SIZE}});
        }
        res.set_content(arr.dump(), "application/json");
    });

    svr.Post("/api/synth/wavetables/create", [this](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        if (!body.is_object() || !body.contains("data") || !body["data"].is_array()
            || body["data"].empty() || body["data"].size()>SYNTH_WAVETABLE_SIZE) {
            res.status=400;
            res.set_content(R"({"ok":false,"error":"Se requiere data con 1-256 muestras"})","application/json");
            return;
        }
        const auto current=engine.getWavetableSnapshot();
        if (current && current->size()>=64) {
            res.status=409;
            res.set_content(R"raw({"ok":false,"error":"Banco wavetable lleno (64 tablas)"})raw", "application/json");
            return;
        }
        Wavetable wt;
        wt.name=(body.contains("name") && body["name"].is_string())
                    ?body["name"].get<std::string>().substr(0,64):"Custom";
        if(wt.name.empty())wt.name="Custom";
        wt.custom=true;
        for(size_t i=0;i<body["data"].size();++i){
            if(!body["data"][i].is_number()){
                res.status=400;
                res.set_content(R"({"ok":false,"error":"Las muestras deben ser numericas"})","application/json");
                return;
            }
            const double v=body["data"][i].get<double>();
            if(!std::isfinite(v)){
                res.status=400;
                res.set_content(R"({"ok":false,"error":"La tabla contiene valores no finitos"})","application/json");
                return;
            }
            wt.data[i]=std::clamp(static_cast<float>(v),-1.f,1.f);
        }
        engine.addWavetable(wt);
        const auto updated=engine.getWavetableSnapshot();
        res.set_content(json{{"ok",true},{"index",updated ? updated->size()-1 : 0},
                             {"count",updated ? updated->size() : 0}}.dump(), "application/json");
    });
}
