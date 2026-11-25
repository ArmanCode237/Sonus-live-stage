// ACERCA DE (About): application info and the configurable links (config/links.json).
//   GET  /api/about                 -> {name, version, host, dataDir, links:{support, website, source}}
//   POST /api/about/open {link}     -> opens that CONFIGURED link in the system's default browser
// The engine opens the browser (not the page): from the app's window or a DAW's plug-in editor,
// a link must leave the embedded WebView. Only the URLs of links.json can be opened, never a URL
// sent by a request.
#include "app/routes/RouteCommon.h"
#include "core/Edition.h"
#if defined(_WIN32)
#include <shellapi.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#ifndef STAGE_VERSION
#define STAGE_VERSION "dev"
#endif

using namespace SonusDSP;

namespace {
/** links.json next to the bundled assets (ui/..), then a user copy in the data folder (wins). */
json readLinks() {
    json links = {{"support", ""}, {"website", ""}, {"source", ""}};
    const fs::path candidates[] = {fs::path(findUiDir()).parent_path() / "config/links.json",
                                   stageUserDataDir() / "links.json"};
    for (const auto& file : candidates) {
        std::ifstream in(file);
        if (!in) continue;
        const auto j = json::parse(in, nullptr, false);
        if (!j.is_object()) continue;
        for (const auto& [key, field] : {std::pair<const char*, const char*>{"support", "supportUrl"},
                                         {"website", "websiteUrl"}, {"source", "sourceUrl"}}) {
            const auto v = j.value(field, std::string());
            if (!v.empty()) links[key] = v;
        }
    }
    return links;
}

/** Only absolute http(s) URLs without spaces or control characters. */
bool safeUrl(const std::string& url) {
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) return false;
    if (url.size() > 2048) return false;
    return std::none_of(url.begin(), url.end(), [](unsigned char c) { return c <= 0x20 || c == 0x7F || c == '"'; });
}

bool openInSystemBrowser(const std::string& url) {
#if defined(_WIN32)
    const int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (n <= 0) return false;
    std::wstring wide(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wide.data(), n);
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#elif defined(__APPLE__) || defined(__linux__)
#if defined(__APPLE__)
    const char* tool = "open";
#else
    const char* tool = "xdg-open";
#endif
    // posix_spawn, not a shell: the URL is one argument, never interpreted.
    pid_t pid = 0;
    char* argv[] = {const_cast<char*>(tool), const_cast<char*>(url.c_str()), nullptr};
    if (posix_spawnp(&pid, tool, nullptr, nullptr, argv, environ) != 0) return false;
    int status = 0;
    waitpid(pid, &status, 0);
    return true;
#else
    (void)url;
    return false;
#endif
}
}

void StageApp::registerAboutRoutes() {
    svr.Get("/api/about", [this](const httplib::Request&, httplib::Response& res) {
        const json out = {
            {"name", "Sonus Live Stage"}, {"version", STAGE_VERSION},
            {"host", hw ? hw->hostKind() : "app"},
            {"dataDir", stageUserDataDir().string()},
            {"license", "GNU AGPL-3.0-or-later"},
            {"edition", Edition::kName},
            {"links", readLinks()},
        };
        res.set_content(out.dump(), "application/json");
    });
    svr.Post("/api/about/open", [](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        const std::string which = body.is_object() ? body.value("link", std::string()) : std::string();
        const auto links = readLinks();
        if (!links.contains(which) || !links[which].is_string()) {
            res.status = 400; res.set_content(R"({"ok":false,"error":"Enlace desconocido"})", "application/json"); return;
        }
        const std::string url = links[which].get<std::string>();
        if (url.empty()) {
            res.status = 404; res.set_content(R"json({"ok":false,"error":"Este enlace todavía no está configurado (config/links.json)"})json", "application/json"); return;
        }
        if (!safeUrl(url)) {
            res.status = 422; res.set_content(R"({"ok":false,"error":"El enlace configurado no es una URL http(s) válida"})", "application/json"); return;
        }
        const bool ok = openInSystemBrowser(url);
        if (!ok) res.status = 500;
        res.set_content(json({{"ok", ok}, {"url", url}}).dump(), "application/json");
    });
}
