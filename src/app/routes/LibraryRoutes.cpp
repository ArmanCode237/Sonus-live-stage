// Generated from the former monolithic main.cpp: route bodies are unchanged.
#include "app/routes/RouteCommon.h"

using namespace SonusDSP;

bool StageApp::localLibraryRequest(const httplib::Request& r){
    const auto& ip=r.remote_addr;
    return ip=="127.0.0.1"||ip=="::1"||ip=="::ffff:127.0.0.1";
}

void StageApp::registerLibraryRoutes() {
    svr.Get("/api/sample-library/status",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;res.set_content(R"({"ok":false,"error":"Solo localhost"})","application/json");return;}
        json config;{std::lock_guard<std::mutex> g(sampleLibraryMutex);config=sampleLibraryConfig;}
        res.set_content(json{{"ok",true},{"configuration",config},
            {"progress",libraryProgressJson()}}.dump(),"application/json");
    });

    svr.Get("/api/sample-library/items",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;res.set_content(R"({"ok":false,"error":"Solo localhost"})","application/json");return;}
        const auto indexed=libraryIndexItems(req.has_param("rescan"));json out=json::array();
        for(const auto& entry:indexed){
            if(!entry.is_object())continue;
            const auto path=entry.value("path",std::string{});
            const auto ext=entry.value("ext",std::string{});
            const bool online=fs::is_regular_file(path);
            const bool cacheable=(ext==".npno"||ext==".nsp")||DecodedSampleCache::supports(path);
            const bool cached=online && ((ext==".npno"||ext==".nsp")?BankLoader::hasNpnoCache(path):
                (cacheable&&DecodedSampleCache::containsFresh(path)));
            auto view=entry;
            view["cacheable"]=cacheable;view["cached"]=cached;view["online"]=online;
            out.push_back(std::move(view));
        }
        res.set_content(json{{"ok",true},{"items",out},{"count",out.size()}}.dump(),"application/json");
    });

    svr.Post("/api/sample-library/source",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;res.set_content(R"({"ok":false,"error":"Solo localhost"})","application/json");return;}
        const auto body=json::parse(req.body,nullptr,false);
        if(!body.is_object()){res.status=400;return;}
        const auto type=body.value("type",std::string{}),action=body.value("action",std::string("add"));
        const auto requested=body.value("path",std::string{});
        if((type!="root"&&type!="file")||(action!="add"&&action!="remove")||requested.empty()){
            res.status=400;res.set_content(R"({"ok":false,"error":"Parámetros inválidos"})","application/json");return;
        }
        const std::string path=canonicalSamplePath(requested);
        const auto supportedFile=[&](){auto ext=fs::path(path).extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
            return ext==".npno"||ext==".nsp"||ext==".nsmp"||ext==".nsmp3"||
                ext==".nsmp4"||ext==".sf2"||ext==".sfz";
        };
        if(path.empty()||(action=="add"&&!(type=="root"?fs::is_directory(path):
            (fs::is_regular_file(path)&&supportedFile())))){
            res.status=400;res.set_content(R"({"ok":false,"error":"Ruta inaccesible o inválida"})","application/json");return;
        }
        json config;
        {std::lock_guard<std::mutex> guard(sampleLibraryMutex);
         config=sampleLibraryConfig;auto& arr=config[type=="root"?"roots":"files"];
         json filtered=json::array();
         for(const auto& element:arr)if(element.is_string()&&canonicalSamplePath(element.get<std::string>())!=path)filtered.push_back(element);
         if(action=="add")filtered.push_back(path);
         arr=std::move(filtered);
         if(!saveJsonAtomic(sampleLibraryFile,config)){res.status=500;res.set_content(R"({"ok":false,"error":"No se pudo guardar el índice"})","application/json");return;}
         sampleLibraryConfig=config;
        }
        res.set_content(json{{"ok",true},{"configuration",config}}.dump(),"application/json");
    });

    svr.Post("/api/sample-library/cache-directory",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        const auto body=json::parse(req.body,nullptr,false);
        if(!body.is_object()){res.status=400;return;}
        const fs::path path=body.value("path",std::string{});
        if(path.empty()||!path.is_absolute()||sampleLibraryWorker.progress().running){
            res.status=400;res.set_content(R"({"ok":false,"error":"Usa una ruta absoluta y detén la preparación primero"})","application/json");return;
        }
        std::error_code ec;fs::create_directories(path,ec);
        if(ec){res.status=400;res.set_content(R"({"ok":false,"error":"No se pudo abrir la carpeta"})","application/json");return;}
        {std::lock_guard<std::mutex> lock(sampleLibraryMutex);
         auto config=sampleLibraryConfig;config["cacheDir"]=path.string();
         if(!saveJsonAtomic(sampleLibraryFile,config)){res.status=500;return;}
         sampleLibraryConfig=config;DecodedSampleCache::setDirectory(path);
         BankLoader::setNpnoCacheDirectory(path);}
        res.set_content(R"({"ok":true})","application/json");
    });

    svr.Post("/api/sample-library/prepare",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        std::vector<std::string> sources;
        for(const auto& entry:registeredEntries(true))
            if(entry.extension==".npno"||entry.extension==".nsp"||DecodedSampleCache::supports(entry.path))sources.push_back(entry.path);
        const bool started=sampleLibraryWorker.prepareAsync(std::move(sources));
        if(!started)res.status=409;
        res.set_content(json{{"ok",started},{"progress",libraryProgressJson()},
            {"message",started?"Preparación iniciada":"Ya existe una preparación o no se encontraron archivos compatibles"}}.dump(),"application/json");
    });

    svr.Post("/api/sample-library/cancel",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        sampleLibraryWorker.cancel();
        res.set_content(json{{"ok",true},{"progress",libraryProgressJson()}}.dump(),"application/json");
    });

    // Explicit repair for a renamed or moved library. No source bytes change.
    // Existing caches from v28.1 are re-keyed only after validating their
    // recorded size/mtime; SFZ is re-prepared due to relocatable WAV dependencies.
    svr.Post("/api/sample-library/relocate",[this](const httplib::Request& req,httplib::Response& res){
        if(!localLibraryRequest(req)){res.status=403;return;}
        if(sampleLibraryWorker.progress().running){res.status=409;
            res.set_content(R"({"ok":false,"error":"Cancela la preparación antes de reubicar"})","application/json");return;}
        const auto request=json::parse(req.body,nullptr,false);
        if(!request.is_object()){res.status=400;return;}
        const fs::path oldRoot=fs::path(request.value("oldRoot",std::string{})).lexically_normal();
        const fs::path newRoot=fs::path(request.value("newRoot",std::string{})).lexically_normal();
        if(!oldRoot.is_absolute()||!newRoot.is_absolute()||oldRoot==newRoot||
           oldRoot==oldRoot.root_path()||!fs::is_directory(newRoot)){
            res.status=400;res.set_content(R"({"ok":false,"error":"Indica la ruta absoluta anterior y la nueva carpeta existente"})","application/json");return;
        }
        std::lock_guard<std::mutex> programGuard(dataMutex);
        std::lock_guard<std::mutex> libraryGuard(sampleLibraryMutex);
        json changedConfig=sampleLibraryConfig,changedPrograms=userPrograms,changedSounds=soundLibrary;
        // Piano Performance references are scoped independently from Global Programs.
        // Include them in library relocation without silently dropping their snapshots.
        const fs::path pianoPerformanceFile=stageUserDataDir()/"piano_performances.json";
        json changedPianoPerformances=json::array();
        bool pianoPerformanceExists=false;
        {
            std::error_code ec;
            pianoPerformanceExists=fs::is_regular_file(pianoPerformanceFile,ec);
            if(pianoPerformanceExists){
                try{
                    std::ifstream in(pianoPerformanceFile);
                    changedPianoPerformances=json::parse(in);
                    if(!changedPianoPerformances.is_array())throw std::runtime_error("Invalid Piano Performance index");
                }catch(...){
                    res.status=500;
                    res.set_content(R"({"ok":false,"error":"El índice de Piano Performances no se puede leer; no se modificó ningún archivo."})","application/json");
                    return;
                }
            }
        }
        const json oldPianoPerformances=changedPianoPerformances;
        auto suffixInside=[&](const fs::path& input)->std::optional<fs::path>{
            const auto path=input.lexically_normal();
            const auto relative=path.lexically_relative(oldRoot);
            if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")return std::nullopt;
            return relative;
        };
        bool registered=(fs::path(libraryRootDir).lexically_normal()==oldRoot);
        for(const auto& key:{"roots","files"}){
            if(!changedConfig.contains(key)||!changedConfig[key].is_array())continue;
            for(const auto& value:changedConfig[key])if(value.is_string()&&
                suffixInside(fs::path(value.get<std::string>())))registered=true;
        }
        if(!registered){
            // Also accept an old source root still referenced by a saved program.
            std::function<bool(const json&)> referencesOld=[&](const json& j)->bool{
                if(j.is_array()){
                    for(const auto& x:j)if(referencesOld(x))return true;
                }else if(j.is_object()){
                    for(auto it=j.begin();it!=j.end();++it){
                        if((it.key()=="sourcePath"||it.key()=="samplePath")&&it.value().is_string()&&
                           suffixInside(fs::path(it.value().get<std::string>())))return true;
                        if((it.value().is_object()||it.value().is_array())&&referencesOld(it.value()))return true;
                    }
                }
                return false;
            };
            registered=referencesOld(changedPrograms)||referencesOld(changedSounds)||referencesOld(changedPianoPerformances);
        }
        if(!registered){res.status=400;
            res.set_content(R"({"ok":false,"error":"La carpeta anterior no está registrada. Agrégala primero o revisa la ruta."})","application/json");return;}
        std::set<std::pair<std::string,std::string>> movedFiles;
        auto moveSource=[&](std::string& path)->bool{
            std::string newPath;
            if(!relocateSourcePath(path,oldRoot,newRoot,newPath))return false;
            movedFiles.emplace(path,newPath);path=newPath;return true;
        };
        std::function<void(json&)> visitPaths=[&](json& node){
            if(node.is_array()){for(auto& v:node)visitPaths(v);return;}
            if(!node.is_object())return;
            for(auto it=node.begin();it!=node.end();++it){
                if((it.key()=="sourcePath"||it.key()=="samplePath")&&it.value().is_string()){
                    std::string value=it.value().get<std::string>();
                    if(moveSource(value))it.value()=value;
                }else if(it.value().is_array()||it.value().is_object())visitPaths(it.value());
            }
        };
        visitPaths(changedPrograms);visitPaths(changedSounds);visitPaths(changedPianoPerformances);
        // Add indexed assets that may not currently belong to a Program/Sound.
        for(const auto& item:indexedItems)if(item.is_object()&&item.value("path",json()).is_string()){
            auto path=item.value("path",std::string{});moveSource(path);
        }
        // Handle a user who already pressed Refresh after renaming the root:
        // the old index may have disappeared, but source-relative filenames
        // are recoverable from the newly supplied folder.
        for(const auto& entry:BankLoader::scanLibraryDirectory(newRoot.generic_string())){
            const auto relative=fs::path(entry.path).lexically_relative(newRoot);
            if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")continue;
            std::string previous=(oldRoot/relative).generic_string();moveSource(previous);
        }
        json legacy=json::array();
        {std::ifstream file(stageUserPresetFile("piano"));
         if(file)try{legacy=json::parse(file);if(!legacy.is_array())legacy=json::array();}catch(...){}}

        const json oldLegacy=legacy;
        visitPaths(legacy);
        // Roots and individually registered files are changed independently.
        for(auto& value:changedConfig["roots"])if(value.is_string()){
            const auto rel=suffixInside(fs::path(value.get<std::string>()));
            if(rel&&fs::is_directory(newRoot/(*rel)))value=(newRoot/(*rel)).generic_string();
        }
        for(auto& value:changedConfig["files"])if(value.is_string()){
            std::string path=value.get<std::string>();if(moveSource(path))value=path;
        }
        // If the configured SSD cache itself was inside the renamed folder,
        // follow that directory before re-keying its files.
        const auto previousCacheDir=fs::path(sampleLibraryConfig.value("cacheDir",std::string{}));
        bool movedCacheDir=false;
        if(const auto rel=suffixInside(previousCacheDir)){
            const auto dest=newRoot/(*rel);
            if(fs::is_directory(dest)){
                movedCacheDir=true;changedConfig["cacheDir"]=dest.generic_string();
                DecodedSampleCache::setDirectory(dest);BankLoader::setNpnoCacheDirectory(dest);
            }
        }
        // Re-key validated SSD cache images without decoding; SFZ must be rebuilt.
        size_t reused=0,needsRebuild=0;
        for(const auto& [oldFile,newFile]:movedFiles){
            auto ext=fs::path(newFile).extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(ext==".npno"||ext==".nsp"){
                if(BankLoader::rebindNpnoCache(oldFile,newFile))++reused;
                else ++needsRebuild;
            }else if(ext==".sf2"){
                bool any=DecodedSampleCache::rebindRelocated(oldFile,newFile,-1);
                for(int i=0;i<1024;++i)
                    if(DecodedSampleCache::rebindRelocated(oldFile,newFile,i))any=true;
                if(any)++reused;else ++needsRebuild;
            }else if(DecodedSampleCache::supports(newFile)){
                if(DecodedSampleCache::rebindRelocated(oldFile,newFile))++reused;
                else ++needsRebuild;
            }
        }
        // Keep an untouched backup before rewriting user-owned snapshots.
        auto backup=[](const fs::path& p)->bool{
            std::error_code ec;
            if(!fs::is_regular_file(p,ec))return !ec;
            fs::copy_file(p,p.string()+".before-v28-2.bak",
                fs::copy_options::skip_existing,ec);
            return !ec;
        };
        const auto legacyPath=stageUserPresetFile("piano");
        if(!backup(programsFilePath)||!backup(soundsFilePath)||
           (changedPianoPerformances!=oldPianoPerformances&&!backup(pianoPerformanceFile))||
           (legacy!=oldLegacy&&!backup(legacyPath))){
            if(movedCacheDir){DecodedSampleCache::setDirectory(previousCacheDir);BankLoader::setNpnoCacheDirectory(previousCacheDir);}
            res.status=500;res.set_content(R"({"ok":false,"error":"No se pudo crear el respaldo de los programas y Sounds."})","application/json");return;
        }
        if(!saveJsonAtomic(programsFilePath,changedPrograms)||
           !saveJsonAtomic(soundsFilePath,changedSounds)||
           (changedPianoPerformances!=oldPianoPerformances&&!saveJsonAtomic(pianoPerformanceFile,changedPianoPerformances))||
           (legacy!=oldLegacy&&!saveJsonAtomic(legacyPath,legacy))||
           !saveJsonAtomic(sampleLibraryFile,changedConfig)){
            if(movedCacheDir){DecodedSampleCache::setDirectory(previousCacheDir);BankLoader::setNpnoCacheDirectory(previousCacheDir);}
            res.status=500;res.set_content(R"({"ok":false,"error":"No se pudieron persistir todas las referencias: comprueba las copias de seguridad."})","application/json");return;
        }
        json history=json::array();
        {std::lock_guard<std::mutex> g(relocatedPathsMutex);
         for(const auto& pair:relocatedPaths)history.push_back({{"oldRoot",pair.first.generic_string()},{"newRoot",pair.second.generic_string()}});}
        history.push_back({{"oldRoot",oldRoot.generic_string()},{"newRoot",newRoot.generic_string()}});
        saveJsonAtomic(relocationFile,history);
        {std::lock_guard<std::mutex> g(relocatedPathsMutex);relocatedPaths.emplace_back(oldRoot,newRoot);}
        sampleLibraryConfig=changedConfig;userPrograms=changedPrograms;soundLibrary=changedSounds;
        if(fs::path(libraryRootDir).lexically_normal()==oldRoot)libraryRootDir=newRoot.generic_string();
        {std::lock_guard<std::mutex> g(libraryIndexMutex);indexedSignature.clear();indexedItems=json::array();}
        res.set_content(json{{"ok",true},{"updatedFiles",movedFiles.size()},
            {"reusedCaches",reused},{"requiresRebuild",needsRebuild},
            {"message","Rutas reubicadas; las dependencias SFZ se preparan de nuevo desde la biblioteca."}}.dump(),"application/json");
    });

    svr.Get("/api/library", [this](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("dir")) {
            std::string reqDir = req.get_param_value("dir");
            if (!reqDir.empty()) libraryRootDir = reqDir;
        }
        auto items = registeredEntries(true);
        std::lock_guard<std::mutex> lock(dataMutex);

        json arr = json::array();
        for (const auto& it : items) {
            std::string group = it.instrumentGroup;
            // Non-destructive filename heuristics. Explicit user tags override these defaults.
            const auto lowered = [&]{ std::string name=it.name; std::transform(name.begin(),name.end(),name.begin(),
                [](unsigned char c){return static_cast<char>(std::tolower(c));}); return name; }();
            const auto has = [&](const char* token){return lowered.find(token)!=std::string::npos;};
            if (has("bell") || has("chime")) group="Bells & Chimes";
            else if (has("pad") || has("ambient") || has("atmos")) group="Pads & Atmospheres";
            else if (has("lead")) group="Synth Leads";
            else if (has("bass") || has("sub")) group="Bass & Sub";
            else if (has("pluck")) group="Plucks & Arpeggios";
            else if (has("brass") || has("horn") || has("trumpet")) group="Brass";
            else if (has("string") || has("violin") || has("cello")) group="Strings";
            else if (has("choir") || has("vox") || has("vocal")) group="Choirs & Vocals";
            else if (has("guitar") || has("ukulele")) group="Guitars & Plucked";
            else if (has("drum") || has("percussion") || has("perc")) group="Drums & Percussion";
            else if (has("rhodes") || has("wurlitzer") || has("clav")) group="Electric Pianos";
            bool favorite = false;
            if (sampleTags.contains(it.name) && sampleTags[it.name].is_object()) {
                group = sampleTags[it.name].value("group", group);
                favorite = sampleTags[it.name].value("favorite", false);
            }
            arr.push_back({
                {"name", it.name},
                {"path", it.path},
                {"ext", it.extension},
                {"category", it.category},
                {"group", group},
                {"favorite", favorite},
                {"sizeMB", static_cast<double>(it.sizeBytes) / (1024.0 * 1024.0)},
                {"ready", it.hasAudioReady}
            });
        }
        res.set_content(json({{"rootDir", libraryRootDir}, {"items", arr}}).dump(), "application/json");
    });

    svr.Post("/api/library/tag", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto b = json::parse(req.body, nullptr, false);
        std::string fileName = b.value("name", "");
        if (!fileName.empty()) {
            if (!sampleTags.contains(fileName) || !sampleTags[fileName].is_object()) {
                sampleTags[fileName] = json::object();
            }
            if (b.contains("group")) sampleTags[fileName]["group"] = b.value("group", "Acoustic Pianos");
            if (b.contains("favorite")) sampleTags[fileName]["favorite"] = b.value("favorite", false);
            std::ofstream tf(tagsFilePath);
            if (tf) tf << sampleTags.dump(2);
        }
        res.set_content("{\"ok\":true}", "application/json");
    });

    svr.Get("/api/sf2/presets", [this](const httplib::Request& req, httplib::Response& res){
        const auto path=req.has_param("path")?req.get_param_value("path"):std::string{};
        json names=json::array();
        if(fs::path(path).extension()==".sf2" && fs::is_regular_file(path)){
            for(const auto& name:Sf2Loader::listPresets(path))names.push_back(name);
        }
        res.set_content(json{{"ok",!names.empty()},{"presets",names}}.dump(),"application/json");
    });
}
