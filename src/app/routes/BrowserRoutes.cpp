// v32.4 sound browser: organisation metadata for every sound of the three modules and
// rename / duplicate of user presets. Loading sounds keeps using each module's routes.
//
// browser_meta.json (user data):
//   { "version":1,
//     "items":  { "<key>": { "tags":[...], "category":"...", "favorite":true } },
//     "usage":  { "<key>": { "count":N, "last":<epoch ms> } } }
// Keys: synth:f:<id> · synth:u:<name> · synth:dx7:<bank>:<voice> · synth:d50:<bank>:<n> ·
//       synth:mt32:<program> · organ:f:<id> · organ:u:<name> · piano:lib:<path> ·
//       piano:snd:<id> · piano:perf:<name>
#include "app/routes/RouteCommon.h"
#include <chrono>

using namespace SonusDSP;

namespace {
json normaliseTags(const json& in) {
    json out = json::array();
    if (!in.is_array()) return out;
    for (const auto& t : in) {
        if (!t.is_string()) continue;
        std::string s = t.get<std::string>();
        const auto b = s.find_first_not_of(" \t#"), e = s.find_last_not_of(" \t");
        if (b == std::string::npos) continue;
        s = s.substr(b, e - b + 1);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (s.size() > 32) s.resize(32);
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
        if (out.size() >= 16) break;
    }
    return out;
}
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

void StageApp::registerBrowserRoutes() {
    const fs::path metaFile = stageUserDataDir() / "browser_meta.json";
    auto loadMeta = [metaFile]() {
        json meta;
        if (std::ifstream in(metaFile); in) meta = json::parse(in, nullptr, false);
        if (!meta.is_object()) meta = json::object();
        if (!meta.contains("items") || !meta["items"].is_object()) meta["items"] = json::object();
        if (!meta.contains("usage") || !meta["usage"].is_object()) meta["usage"] = json::object();
        meta["version"] = 1;
        return meta;
    };

    svr.Get("/api/browser/meta", [this, loadMeta](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        res.set_content(loadMeta().dump(), "application/json");
    });

    svr.Post("/api/browser/meta", [this, loadMeta, metaFile](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        if (!body.is_object()) { res.status = 400; res.set_content(R"({"ok":false})", "application/json"); return; }
        const std::string op = body.value("op", std::string());
        const std::string key = body.value("key", std::string());
        std::lock_guard<std::mutex> lock(dataMutex);
        json meta = loadMeta();
        auto& items = meta["items"];
        auto& usage = meta["usage"];
        auto item = [&]() -> json& { if (!items.contains(key) || !items[key].is_object()) items[key] = json::object(); return items[key]; };
        if (key.empty() && op != "rename") { res.status = 400; res.set_content(R"({"ok":false,"error":"Falta la clave"})", "application/json"); return; }
        if (op == "tags") item()["tags"] = normaliseTags(body.value("tags", json::array()));
        else if (op == "category") {
            std::string c = body.value("category", std::string());
            std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
            if (c.size() > 24) c.resize(24);
            if (c.empty()) item().erase("category"); else item()["category"] = c;
        }
        else if (op == "favorite") item()["favorite"] = body.value("favorite", false);
        else if (op == "use") {
            auto& u = usage[key];
            if (!u.is_object()) u = json::object();
            u["count"] = u.value("count", 0) + 1;
            u["last"] = nowMs();
        }
        else if (op == "rename") {
            const std::string from = body.value("from", std::string()), to = body.value("to", std::string());
            if (!from.empty() && !to.empty()) {
                if (items.contains(from)) { items[to] = items[from]; items.erase(from); }
                if (usage.contains(from)) { usage[to] = usage[from]; usage.erase(from); }
            }
        }
        else if (op == "forget") { items.erase(key); usage.erase(key); }
        else { res.status = 400; res.set_content(R"({"ok":false,"error":"Operación desconocida"})", "application/json"); return; }
        // Drop empty records so the file stays small.
        if (items.contains(key) && items[key].is_object()) {
            auto& it = items[key];
            if (it.contains("tags") && it["tags"].empty()) it.erase("tags");
            if (it.contains("favorite") && !it["favorite"].get<bool>()) it.erase("favorite");
            if (it.empty()) items.erase(key);
        }
        if (!saveJsonAtomic(metaFile.string(), meta)) { res.status = 500; res.set_content(R"({"ok":false,"error":"No se pudo guardar"})", "application/json"); return; }
        res.set_content(json({{"ok", true}, {"meta", meta}}).dump(), "application/json");
    });

    // Rename / duplicate user presets (synth, organ). The stored sound is copied as it is:
    // the edit buffer is never involved (unlike "save", which captures the current sound).
    auto presetOp = [this](const std::string& path, bool duplicate) {
        return [this, path, duplicate](const httplib::Request& req, httplib::Response& res) {
            const auto body = json::parse(req.body, nullptr, false);
            std::string from = body.is_object() ? body.value("name", std::string()) : std::string();
            std::string to = body.is_object() ? body.value("to", std::string()) : std::string();
            if (!checkPresetName(to)) { res.status = 400; res.set_content(R"({"ok":false,"error":"Nombre vacío"})", "application/json"); return; }
            std::lock_guard<std::mutex> lock(dataMutex);
            json presets = loadPresetsFile(path);
            auto named = [&](const std::string& n) {
                return std::find_if(presets.begin(), presets.end(), [&](const json& p) { return p.is_object() && p.value("name", std::string()) == n; });
            };
            auto source = named(from);
            if (source == presets.end()) { res.status = 404; res.set_content(R"({"ok":false,"error":"No existe ese preset"})", "application/json"); return; }
            if (named(to) != presets.end()) { res.status = 409; res.set_content(R"({"ok":false,"error":"Ya existe otro preset con ese nombre"})", "application/json"); return; }
            if (duplicate) { json copy = *source; copy["name"] = to; copy["favorite"] = false; presets.push_back(copy); }
            else (*source)["name"] = to;
            const bool ok = saveJsonAtomic(path, presets);
            if (!ok) res.status = 500;
            // A renamed preset that is playing keeps its name on the displays.
            const char* refKey = path == synthPresetsPath ? "synthPreset" : "organPreset";
            if (ok && !duplicate && presetRefName(selectedSoundRefs, refKey) == from)
                selectedSoundRefs[refKey]["name"] = to;
            res.set_content(json({{"ok", ok}, {"presets", presets}}).dump(), "application/json");
        };
    };
    svr.Post("/api/synth/presets/rename", presetOp(synthPresetsPath, false));
    svr.Post("/api/synth/presets/duplicate", presetOp(synthPresetsPath, true));
    svr.Post("/api/organ/presets/rename", presetOp(organPresetsPath, false));
    svr.Post("/api/organ/presets/duplicate", presetOp(organPresetsPath, true));
}
