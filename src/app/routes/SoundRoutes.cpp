// Generated from the former monolithic main.cpp: route bodies are unchanged.
#include "app/routes/RouteCommon.h"

using namespace SonusDSP;

// v28.2 Sound Manager: each Sound owns a module-specific DSP snapshot, not PCM.
// Local-only CRUD. Legacy Piano preset files are never modified by migration.
int StageApp::soundModuleIndex(const std::string& name){
    return name=="piano"?0:name=="synth"?1:name=="organ"?2:-1;
}

std::string StageApp::newSoundId(){
    static std::atomic<uint64_t> seq{0};
    auto h=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream s;s<<"sound_"<<std::hex<<h<<"_"<<seq.fetch_add(1);
    return s.str();
}

json StageApp::soundSummary(const json& sound){
    json out=json::object();
    for(const char* k:{"id","name","module","category","created","updated","source","tags","scope","savedLayer","defaultForSource"})
        if(sound.contains(k))out[k]=sound[k];
    if(sound.value("module",std::string{})=="piano"&&sound.value("patch",json::object()).is_object()){
        const auto& patch=sound["patch"];
        if(sound.value("scope",std::string{})=="layer"&&patch.contains("source")&&patch["source"].is_object()){
            const auto& src=patch["source"];
            out["instruments"]=json::array({src.value("name",std::string{})});
        }
        if(patch.contains("pianoSources")&&patch["pianoSources"].is_array()){
            out["instruments"]=json::array();
            for(const auto& layer:patch["pianoSources"])
                if(layer.is_object()&&!layer.value("sourcePath",std::string{}).empty())
                    out["instruments"].push_back(layer.value("name",std::string{}));
        }
    }
    return out;
}

void StageApp::registerSoundRoutes() {
    // The former two-/three-Sound builder modified global programs and
    // switched off unrelated modules. It is deliberately retired in v28.2.4.
    svr.Post("/api/performances/store",[this](const httplib::Request&,httplib::Response& res){
        res.status=409;
        res.set_content(R"({"ok":false,"error":"Utiliza /api/piano/performances/store para guardar las cinco capas de Piano."})","application/json");
    });

    svr.Get("/api/sounds",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        const auto filter=req.has_param("module")?req.get_param_value("module"):std::string{};
        std::lock_guard<std::mutex> guard(dataMutex);
        json items=json::array();
        for(const auto& snd:soundLibrary["sounds"])
            if(snd.is_object()&&(filter.empty()||snd.value("module",std::string{})==filter))
                items.push_back(soundSummary(snd));
        res.set_content(json{{"ok",true},{"sounds",items},{"selected",selectedSoundRefs}}.dump(),"application/json");
    });

    svr.Post("/api/sounds/save",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()){res.status=400;return;}
        auto module=b.value("module",std::string{});const int mi=soundModuleIndex(module);
        std::string name=b.value("name",std::string{});
        if(mi<0||!checkPresetName(name)){res.status=400;res.set_content(R"({"ok":false,"error":"Módulo o nombre inválido"})","application/json");return;}
        const bool layerSound=module=="piano"&&b.contains("layer");
        if(layerSound&&(!b["layer"].is_number_integer()||b["layer"].get<int>()<0||
                        b["layer"].get<int>()>=PIANO_LAYER_COUNT)){
            res.status=400;res.set_content(R"({"ok":false,"error":"Capa inválida"})","application/json");return;
        }
        const int soundLayer=layerSound?b["layer"].get<int>():0;
        std::lock_guard<std::mutex> guard(dataMutex);
        if(layerSound&&!engine.getLayerInstrument(soundLayer)){
            res.status=409;res.set_content(R"({"ok":false,"error":"Carga primero un sample en la capa elegida para crear su Sound"})","application/json");return;
        }
        auto updated=soundLibrary;auto& bank=updated["sounds"];
        std::string id=b.value("id",std::string{});json* target=nullptr;
        const bool bindToSource=layerSound&&b.value("bindToSource",!b.value("variant",false));
        const auto sourceBank=layerSound?engine.getLayerInstrument(soundLayer):nullptr;
        if(id.empty()&&bindToSource&&sourceBank){
            for(auto& item:bank)if(sourceMatchesSound(item,sourceBank->sourcePath,sourceBank->sf2PresetIndex)&&
                item.value("defaultForSource",false)){
                id=item.value("id",std::string{});break;
            }
        }
        if(!id.empty())for(auto& item:bank)if(item.is_object()&&item.value("id",std::string{})==id){target=&item;break;}
        if(target&&layerSound&&sourceBank&&!sourceMatchesSound(*target,sourceBank->sourcePath,sourceBank->sf2PresetIndex)){
            res.status=409;res.set_content(R"({"ok":false,"error":"El Sound pertenece a otro archivo. Usa Guardar como nuevo para crear una variante."})","application/json");return;
        }
        if(!id.empty()&&target&&module=="piano"&&
           (target->value("scope",std::string("module"))=="layer")!=layerSound){
            res.status=409;res.set_content(R"({"ok":false,"error":"Sound legado de módulo completo; utiliza Guardar como nuevo para crear un Sound individual"})","application/json");return;
        }
        if(!id.empty()&&(!target||target->value("module",std::string{})!=module)){
            res.status=404;res.set_content(R"({"ok":false,"error":"Sound a actualizar no encontrado"})","application/json");return;
        }
        if(id.empty()){
            if(bank.size()>=2048){res.status=409;res.set_content(R"({"ok":false,"error":"La biblioteca alcanzó su límite de 2048 sonidos"})","application/json");return;}
            id=newSoundId();bank.push_back(json::object());target=&bank.back();
            (*target)["id"]=id;(*target)["created"]=std::chrono::system_clock::now().time_since_epoch().count();
        }
        (*target)["name"]=name;(*target)["module"]=module;
        (*target)["category"]=b.value("category",std::string("User" )).substr(0,64);
        (*target)["updated"]=std::chrono::system_clock::now().time_since_epoch().count();
        (*target)["source"]="user";
        (*target)["scope"]=layerSound?"layer":"module";
        if(layerSound)(*target)["savedLayer"]=soundLayer;
        else target->erase("savedLayer");
        (*target)["patch"]=layerSound?capturePianoLayerSound(soundLayer):modulePatch(mi);
        if(layerSound&&bindToSource){
            // One default per asset/preset; multiple variants remain independent.
            for(auto& item:bank)if(&item!=target&&sourceMatchesSound(item,sourceBank->sourcePath,sourceBank->sf2PresetIndex))
                item["defaultForSource"]=false;
            (*target)["defaultForSource"]=true;
        }else if(layerSound&& !target->contains("defaultForSource"))(*target)["defaultForSource"]=false;
        if(b.contains("tags")&&b["tags"].is_array()){
            json tags=json::array();
            for(const auto& tag:b["tags"])if(tag.is_string()&&tags.size()<16)
                tags.push_back(tag.get<std::string>().substr(0,32));
            (*target)["tags"]=tags;
        }
        if(!saveJsonAtomic(soundsFilePath,updated)){
            res.status=500;res.set_content(R"({"ok":false,"error":"No se pudo guardar sounds_v2.json"})","application/json");return;
        }
        const auto summary=soundSummary(*target);
        soundLibrary=std::move(updated);
        if(layerSound){ensurePianoSoundRefs();selectedSoundRefs["pianoLayers"][soundLayer]=id;
            selectedSoundRefs["piano"]=nullptr;soundDirty[size_t(soundLayer)].store(false);}
        else selectedSoundRefs[module]=id;
        res.set_content(json{{"ok",true},{"sound",summary}}.dump(),"application/json");
    });

    svr.Post("/api/sounds/load",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()){res.status=400;return;}
        const auto id=b.value("id",std::string{});
        std::lock_guard<std::mutex> guard(dataMutex);
        for(const auto& sound:soundLibrary["sounds"]){
            if(!sound.is_object()||sound.value("id",std::string{})!=id)continue;
            const auto module=sound.value("module",std::string{});
            const int mi=soundModuleIndex(module);
            const auto patch=sound.value("patch",json::object());
            if(mi<0||!patch.is_object()){res.status=422;return;}
            if(mi==0&&sound.value("scope",std::string{})=="layer"){
                int layer=sound.value("savedLayer",0);
                if(b.contains("layer")){
                    if(!b["layer"].is_number_integer()||b["layer"].get<int>()<0||
                       b["layer"].get<int>()>=PIANO_LAYER_COUNT){res.status=400;return;}
                    layer=b["layer"].get<int>();
                }
                if(!restorePianoLayerSound(layer,patch)){
                    res.status=409;res.set_content(R"({"ok":false,"error":"No se pudo restaurar la capa o falta el sample original; reubica la biblioteca"})","application/json");return;
                }
                ensurePianoSoundRefs();selectedSoundRefs["pianoLayers"][layer]=id;
                selectedSoundRefs["piano"]=nullptr;
                res.set_content(json{{"ok",true},{"sound",soundSummary(sound)},{"layer",layer},
                    {"params",serializeParams(engine.getCurrentParams())},
                    {"pianoLayers",serializePianoLayerSources(engine)}}.dump(),"application/json");return;
            }
            // Never silently replace an active piano by silence if its original
            // has been moved and the new directory has not been relinked yet.
            if(mi==0&&patch.contains("pianoSources")&&patch["pianoSources"].is_array()){
                for(const auto& layer:patch["pianoSources"]){
                    if(!layer.is_object())continue;
                    const auto saved=layer.value("sourcePath",std::string{});
                    if(!saved.empty()&&!fs::is_regular_file(resolveSavedSamplePath(saved))){
                        res.status=409;res.set_content(json{{"ok",false},
                            {"error","Archivo de origen no encontrado. Reubica la biblioteca desde SAMPLE LIBRARY."},
                            {"missingPath",saved}}.dump(),"application/json");return;
                    }
                }
            }
            const bool ok=restoreModulePatch(mi,patch);
            if(ok){
                selectedSoundRefs[module]=id;
                if(mi>0)selectedSoundRefs[module+"Preset"]=nullptr;   // a Sound replaces the preset
                if(mi==0){ensurePianoSoundRefs();for(auto& ref:selectedSoundRefs["pianoLayers"])ref=nullptr;}
            }
            auto bank=engine.getInstrument();
            res.set_content(json{{"ok",ok},{"sound",soundSummary(sound)},
                {"params",serializeParams(engine.getCurrentParams())},
                {"pianoLayers",serializePianoLayerSources(engine)},
                {"instrumentName",bank?bank->name:std::string{}}}.dump(),"application/json");return;
        }
        res.status=404;res.set_content(R"({"ok":false,"error":"Sound no encontrado"})","application/json");
    });

    svr.Post("/api/sounds/set-default",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        const auto b=json::parse(req.body,nullptr,false);
        if(!b.is_object()){res.status=400;return;}
        const auto id=b.value("id",std::string{});
        std::lock_guard<std::mutex> guard(dataMutex);
        auto updated=soundLibrary;
        json* chosen=nullptr;
        for(auto& item:updated["sounds"])if(item.is_object()&&item.value("id",std::string{})==id){
            chosen=&item;break;
        }
        if(!chosen||chosen->value("module",std::string{})!="piano"||
           chosen->value("scope",std::string{})!="layer"){
            res.status=404;res.set_content(R"({"ok":false,"error":"Sound individual no encontrado"})","application/json");return;
        }
        const auto path=(*chosen)["patch"]["source"].value("sourcePath",std::string{});
        const int preset=(*chosen)["patch"]["source"].value("sf2Preset",-1);
        for(auto& item:updated["sounds"])if(sourceMatchesSound(item,fs::path(path),preset))
            item["defaultForSource"]=(item.value("id",std::string{})==id);
        if(!saveJsonAtomic(soundsFilePath,updated)){
            res.status=500;res.set_content(R"({"ok":false,"error":"No se pudo guardar la asociación"})","application/json");return;
        }
        soundLibrary=std::move(updated);
        res.set_content(R"({"ok":true})","application/json");
    });

    svr.Post("/api/sounds/delete",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        const auto b=json::parse(req.body,nullptr,false);if(!b.is_object()){res.status=400;return;}
        const auto id=b.value("id",std::string{});std::lock_guard<std::mutex> guard(dataMutex);
        auto updated=soundLibrary;
        auto& bank=updated["sounds"];const auto count=bank.size();
        bank.erase(std::remove_if(bank.begin(),bank.end(),[&](const json& item){
            return item.is_object()&&item.value("id",std::string{})==id;}),bank.end());
        if(bank.size()==count){res.status=404;return;}
        if(!saveJsonAtomic(soundsFilePath,updated)){res.status=500;return;}
        soundLibrary=std::move(updated);
        for(auto& ref:selectedSoundRefs){
            if(ref==id)ref=nullptr;
            if(ref.is_array())for(auto& item:ref)if(item==id)item=nullptr;
        }
        res.set_content(R"({"ok":true})","application/json");
    });

    svr.Post("/api/sounds/duplicate",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        const auto b=json::parse(req.body,nullptr,false);if(!b.is_object()){res.status=400;return;}
        std::string name=b.value("name",std::string{});
        if(!checkPresetName(name)){res.status=400;return;}
        std::lock_guard<std::mutex> guard(dataMutex);
        auto updated=soundLibrary;auto& items=updated["sounds"];
        if(items.size()>=2048){res.status=409;return;}
        for(const auto& snd:items)if(snd.is_object()&&snd.value("id",std::string{})==b.value("id",std::string{})){
            auto copy=snd;copy["id"]=newSoundId();copy["name"]=name;
            copy["created"]=std::chrono::system_clock::now().time_since_epoch().count();
            copy["updated"]=copy["created"];copy["source"]="user";
            copy["defaultForSource"]=false;
            items.push_back(copy);
            if(!saveJsonAtomic(soundsFilePath,updated)){res.status=500;return;}
            soundLibrary=std::move(updated);
            res.set_content(json{{"ok",true},{"sound",soundSummary(copy)}}.dump(),"application/json");return;
        }
        res.status=404;
    });

    svr.Post("/api/sounds/migrate",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        const auto body=json::parse(req.body,nullptr,false);
        const std::string chosen=body.is_object()?body.value("module",std::string("all")):"all";
        if(chosen!="all"&&soundModuleIndex(chosen)<0){res.status=400;return;}
        std::lock_guard<std::mutex> guard(dataMutex);
        auto updated=soundLibrary;auto& items=updated["sounds"];
        int imported=0,ignored=0;
        for(const std::string module:{"piano","synth","organ"}){
            if(chosen!="all"&&chosen!=module)continue;
            const auto legacy=loadPresetsFile(stageUserPresetFile(module.c_str()));
            for(const auto& p:legacy){
                if(!p.is_object()||!p.value("name",json()).is_string()||
                   !p.value("params",json()).is_object()){++ignored;continue;}
                const auto name=p["name"].get<std::string>();
                if(name.empty()||items.size()>=2048){++ignored;continue;}
                bool exists=false;
                for(const auto& existing:items)if(existing.is_object()&&
                    existing.value("legacyModule",std::string{})==module&&
                    existing.value("legacyName",std::string{})==name){exists=true;break;}
                if(exists){++ignored;continue;}
                const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
                items.push_back({{"id",newSoundId()},{"name",name},{"module",module},
                    {"category","Legacy " + module},{"source","legacy-" + module},
                    {"legacyModule",module},{"legacyName",name},
                    {"created",stamp},{"updated",stamp},{"patch",p["params"]}});
                ++imported;
            }
        }
        if(imported&&!saveJsonAtomic(soundsFilePath,updated)){res.status=500;return;}
        if(imported)soundLibrary=std::move(updated);
        res.set_content(json{{"ok",true},{"imported",imported},{"ignored",ignored},
            {"message","Los presets anteriores continúan intactos."}}.dump(),"application/json");
    });
}
