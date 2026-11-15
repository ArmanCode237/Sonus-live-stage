// DX7 library: factory cartridges (presets/synth/dx7/factory, the Dexed banks) and
// user cartridges (<user data>/presets/dx7/user). A voice loaded into a synth layer
// turns that layer into a DX7 (synthesis type 5) played by msfa.
//   GET  /api/synth/dx7/banks                       -> cartridges with their 32 voice names
//   POST /api/synth/dx7/load   {layer, bank, voice} -> load a voice into a synth layer
//   POST /api/synth/dx7/import {name, hex}          -> store a .syx cartridge (hex bytes)
//   POST /api/synth/dx7/delete {bank}               -> remove a user cartridge
//   GET  /api/synth/mt32/status                     -> ROM status + the 128 timbre names
//   POST /api/synth/mt32/load  {layer, program}     -> MT-32 timbre on a synth layer
//   GET  /api/synth/d50/banks                       -> D-50 patch banks found under roms/
//   POST /api/synth/d50/load   {bank, patch}        -> D-50 patch translated to LA32 layers
//   GET  /api/synth/mod                             -> modulation matrix, sources, targets
//   POST /api/synth/mod        {connections:[...]}  -> replace the modulation matrix
#include "app/StageApp.h"
#include "app/UserDataPaths.h"
#include "dsp/Dx7Engine.h"
#include "dsp/Mt32Engine.h"
#include "app/D50Import.h"
#include "modules/SynthModMatrix.h"
#include <algorithm>
#include <fstream>
#include <iterator>

namespace {
fs::path dx7FactoryDir() { return fs::path(findUiDir()).parent_path() / "presets/synth/dx7/factory"; }
fs::path dx7UserDir() {
    const auto dir = stageUserDataDir() / "presets/dx7/user";
    std::error_code ec; fs::create_directories(dir, ec);
    return dir;
}

bool readCartridge(const fs::path& file, std::vector<NordDSP::Dx7Voice>& voices, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) { error = "No se pudo abrir el cartucho"; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return NordDSP::parseDx7Sysex(bytes, voices, error);
}

// "factory/SynprezFM_03" or "user/MyBank" -> file inside its own folder (no path escapes).
bool resolveBank(const std::string& id, fs::path& file) {
    const auto slash = id.find('/');
    if (slash == std::string::npos) return false;
    const std::string source = id.substr(0, slash), name = id.substr(slash + 1);
    if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name.find("..") != std::string::npos) return false;
    if (source == "factory") file = dx7FactoryDir() / (name + ".syx");
    else if (source == "user") file = dx7UserDir() / (name + ".syx");
    else return false;
    return fs::is_regular_file(file);
}

json bankJson(const std::string& source, const fs::path& file) {
    std::vector<NordDSP::Dx7Voice> voices; std::string error;
    if (!readCartridge(file, voices, error)) return json();
    json names = json::array();
    for (const auto& v : voices) names.push_back(NordDSP::dx7VoiceName(v));
    const std::string name = file.stem().string();
    return {{"id", source + "/" + name}, {"name", name}, {"source", source}, {"voices", names}};
}

bool hexToBytes(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2) return false;
    out.clear(); out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const auto byte = hex.substr(i, 2);
        if (!std::isxdigit(static_cast<unsigned char>(byte[0])) || !std::isxdigit(static_cast<unsigned char>(byte[1]))) return false;
        out.push_back(static_cast<uint8_t>(std::stoi(byte, nullptr, 16)));
    }
    return true;
}
// D-50 banks: any D-50 sysex dump under <project>/roms or <user data>/roms (subfolders too).
std::vector<std::pair<std::string, fs::path>> d50Roots() {
    return {{"project", fs::path(findUiDir()).parent_path() / "roms"}, {"user", stageUserDataDir() / "roms"}};
}
bool resolveD50(const std::string& id, fs::path& file) {
    const auto slash = id.find('/');
    if (slash == std::string::npos || id.find("..") != std::string::npos) return false;
    for (const auto& [key, root] : d50Roots()) {
        if (id.substr(0, slash) != key) continue;
        file = root / fs::path(id.substr(slash + 1));
        return fs::is_regular_file(file);
    }
    return false;
}
bool readD50(const fs::path& file, std::vector<D50::Patch>& patches, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return D50::parseBank(bytes, patches, error);
}
} // namespace

void StageApp::registerDx7Routes() {
    svr.Get("/api/synth/d50/banks", [](const httplib::Request&, httplib::Response& res) {
        json banks = json::array();
        for (const auto& [key, root] : d50Roots()) {
            std::error_code ec;
            if (!fs::is_directory(root, ec)) continue;
            for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
                if (ec || !it->is_regular_file() || it->path().extension() != ".syx") continue;
                std::vector<D50::Patch> patches; std::string error;
                if (!readD50(it->path(), patches, error) || patches.empty()) continue;
                json list = json::array();
                for (size_t i = 0; i < patches.size(); ++i)
                    list.push_back({{"n", i + 1}, {"name", D50::patchName(patches[i])}, {"upper", D50::upperToneName(patches[i])},
                                    {"lower", D50::lowerToneName(patches[i])}, {"mode", D50::keyModeName(patches[i])}});
                const auto rel = fs::relative(it->path(), root, ec).generic_string();
                banks.push_back({{"id", key + "/" + rel}, {"name", it->path().parent_path().filename().string() + " · " + it->path().stem().string()},
                                 {"patches", list}});
            }
        }
        res.set_content(json({{"ok", true}, {"banks", banks}}).dump(), "application/json");
    });

    svr.Post("/api/synth/d50/load", [this](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        const std::string bank = body.is_object() ? body.value("bank", std::string()) : std::string();
        const int number = body.is_object() ? body.value("patch", 0) : 0;
        fs::path file; std::vector<D50::Patch> patches; std::string error;
        if (!resolveD50(bank, file) || !readD50(file, patches, error) || number < 1 || number > int(patches.size())) {
            res.status = 400;
            res.set_content(json({{"ok", false}, {"error", error.empty() ? "Banco o patch del D-50 no valido" : error}}).dump(), "application/json");
            return;
        }
        const auto& patch = patches[size_t(number - 1)];
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            if (!restoreModulePatch(1, D50::toSynthParams(patch))) {
                res.status = 500;
                res.set_content(R"json({"ok":false,"error":"No se pudo aplicar el patch"})json", "application/json");
                return;
            }
            engine.applyParamById("synthEnabled", 1.f, false);
            moduleDirty[1].store(true);
            paramRevCounter.fetch_add(1);
        }
        res.set_content(json({{"ok", true}, {"name", D50::patchName(patch)}, {"mode", D50::keyModeName(patch)}}).dump(), "application/json");
    });

    svr.Get("/api/synth/dx7/banks", [](const httplib::Request&, httplib::Response& res) {
        json banks = json::array();
        for (const auto& [source, dir] : {std::pair{std::string("factory"), dx7FactoryDir()}, std::pair{std::string("user"), dx7UserDir()}}) {
            std::error_code ec;
            std::vector<fs::path> files;
            for (const auto& entry : fs::directory_iterator(dir, ec))
                if (entry.is_regular_file() && entry.path().extension() == ".syx") files.push_back(entry.path());
            std::sort(files.begin(), files.end());
            for (const auto& f : files) { auto b = bankJson(source, f); if (!b.is_null()) banks.push_back(b); }
        }
        res.set_content(json({{"ok", true}, {"banks", banks}}).dump(), "application/json");
    });

    svr.Post("/api/synth/dx7/load", [this](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        const int layer = body.is_object() ? body.value("layer", -1) : -1;
        const int index = body.is_object() ? body.value("voice", -1) : -1;
        const std::string bank = body.is_object() ? body.value("bank", std::string()) : std::string();
        fs::path file;
        if (layer < 0 || layer >= SYNTH_LAYER_COUNT || index < 0 || !resolveBank(bank, file)) {
            res.status = 400;
            res.set_content(R"json({"ok":false,"error":"Se requiere layer (0-3), bank y voice validos"})json", "application/json");
            return;
        }
        std::vector<NordDSP::Dx7Voice> voices; std::string error;
        if (!readCartridge(file, voices, error) || index >= int(voices.size())) {
            res.status = 400;
            res.set_content(json({{"ok", false}, {"error", error.empty() ? "Voz fuera de rango" : error}}).dump(), "application/json");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            const std::string prefix = "L" + std::to_string(layer + 1) + "_";
            auto& cfg = engine.multiSynth_.layers[size_t(layer)].cfg;
            cfg.dx7Voice = voices[size_t(index)];
            cfg.dx7Revision = NordDSP::nextDx7Revision();
            engine.applyParamById(prefix + "synthType", 5.f, false);
            engine.applyParamById(prefix + "en", 1.f, false);
            engine.applyParamById("synthEnabled", 1.f, false);
            moduleDirty[1].store(true);
            paramRevCounter.fetch_add(1);
        }
        res.set_content(json({{"ok", true}, {"layer", layer}, {"name", NordDSP::dx7VoiceName(voices[size_t(index)])}}).dump(), "application/json");
    });

    svr.Post("/api/synth/dx7/import", [](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        std::string name = body.is_object() ? body.value("name", std::string()) : std::string();
        const std::string hex = body.is_object() ? body.value("hex", std::string()) : std::string();
        name.erase(std::remove_if(name.begin(), name.end(), [](char c) {
            return !(std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '-' || c == '.');
        }), name.end());
        if (name.size() > 4 && name.substr(name.size() - 4) == ".syx") name.resize(name.size() - 4);
        std::vector<uint8_t> bytes; std::vector<NordDSP::Dx7Voice> voices; std::string error;
        if (name.empty() || !hexToBytes(hex, bytes) || !NordDSP::parseDx7Sysex(bytes, voices, error)) {
            res.status = 400;
            res.set_content(json({{"ok", false}, {"error", error.empty() ? "Archivo .syx no valido" : error}}).dump(), "application/json");
            return;
        }
        const fs::path file = dx7UserDir() / (name + ".syx");
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!out) { res.status = 500; res.set_content(R"({"ok":false,"error":"No se pudo guardar el cartucho"})", "application/json"); return; }
        out.close();
        res.set_content(json({{"ok", true}, {"bank", bankJson("user", file)}}).dump(), "application/json");
    });

    svr.Get("/api/synth/mt32/status", [](const httplib::Request&, httplib::Response& res) {
        const auto st = NordDSP::Mt32Roms::status();
        res.set_content(json({{"ok", true}, {"loaded", st.loaded}, {"control", st.control}, {"pcm", st.pcm},
            {"message", st.message}, {"folders", st.folders}, {"sounds", st.sounds}}).dump(), "application/json");
    });

    svr.Post("/api/synth/mt32/load", [this](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        const int layer = body.is_object() ? body.value("layer", -1) : -1;
        const int program = body.is_object() ? body.value("program", -1) : -1;
        if (layer < 0 || layer >= SYNTH_LAYER_COUNT || program < 0 || program > 127) {
            res.status = 400;
            res.set_content(R"json({"ok":false,"error":"Se requiere layer (0-3) y program (0-127)"})json", "application/json");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            const std::string prefix = "L" + std::to_string(layer + 1) + "_";
            engine.multiSynth_.layers[size_t(layer)].cfg.mt32Program = program;
            engine.applyParamById(prefix + "synthType", 6.f, false);
            engine.applyParamById(prefix + "en", 1.f, false);
            engine.applyParamById("synthEnabled", 1.f, false);
            moduleDirty[1].store(true);
            paramRevCounter.fetch_add(1);
        }
        const auto st = NordDSP::Mt32Roms::status();
        res.set_content(json({{"ok", true}, {"layer", layer}, {"program", program}, {"romsLoaded", st.loaded},
            {"name", program < int(st.sounds.size()) ? st.sounds[size_t(program)] : std::string()}}).dump(), "application/json");
    });

    auto modJson = [this]() {
        json list = json::array();
        for (const auto& c : engine.multiSynth_.getModMatrix())
            list.push_back({{"source", c.source}, {"target", c.target}, {"amount", c.amount}, {"bipolar", c.bipolar}, {"curve", c.curve}});
        json sources = json::array();
        for (int s = 0; s < NordDSP::MOD_SOURCE_COUNT; ++s)
            sources.push_back({{"id", s}, {"name", NordDSP::modSourceName(s)}, {"voice", NordDSP::modSourceIsVoice(s)}, {"bipolar", NordDSP::modSourceIsBipolar(s)}});
        json targets = json::array();
        for (const auto& t : NordDSP::modTargets()) targets.push_back({{"suffix", t.suffix}, {"voice", t.voice != NordDSP::MVT_NONE}});
        return json({{"ok", true}, {"connections", list}, {"sources", sources}, {"targets", targets}, {"max", NordDSP::MOD_MAX_CONNECTIONS}});
    };
    svr.Get("/api/synth/mod", [modJson](const httplib::Request&, httplib::Response& res) {
        res.set_content(modJson().dump(), "application/json");
    });
    svr.Post("/api/synth/mod", [this, modJson](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        if (!body.is_object() || !body.contains("connections") || !body["connections"].is_array()) {
            res.status = 400;
            res.set_content(R"json({"ok":false,"error":"Se requiere connections: []"})json", "application/json");
            return;
        }
        std::vector<ModConnectionSpec> specs;
        for (const auto& c : body["connections"]) if (c.is_object())
            specs.push_back({c.value("source", 0), c.value("target", std::string()), c.value("amount", .25f), c.value("bipolar", false), c.value("curve", 0)});
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            engine.multiSynth_.setModMatrix(specs);
            moduleDirty[1].store(true);
            paramRevCounter.fetch_add(1);
        }
        res.set_content(modJson().dump(), "application/json");
    });

    // INIT / PASTE one synth layer: {layer, config} (config null = default layer).
    svr.Post("/api/synth/layer", [this](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        const int layer = body.is_object() ? body.value("layer", -1) : -1;
        if (layer < 0 || layer >= SYNTH_LAYER_COUNT) {
            res.status = 400;
            res.set_content(R"json({"ok":false,"error":"Se requiere layer (0-3)"})json", "application/json");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            const json config = body.contains("config") && body["config"].is_object() ? body["config"] : json::object();
            engine.applySynthLayerJson(size_t(layer), config);
            moduleDirty[1].store(true);
            paramRevCounter.fetch_add(1);
        }
        res.set_content(R"json({"ok":true})json", "application/json");
    });

    svr.Post("/api/synth/dx7/delete", [](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        const std::string bank = body.is_object() ? body.value("bank", std::string()) : std::string();
        fs::path file;
        if (bank.rfind("user/", 0) != 0 || !resolveBank(bank, file)) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Solo se pueden borrar cartuchos de usuario"})", "application/json");
            return;
        }
        std::error_code ec; fs::remove(file, ec);
        res.set_content(json({{"ok", !ec}}).dump(), "application/json");
    });
}
