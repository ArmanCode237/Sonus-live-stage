// HTML pages, static UI assets and the request guard shared by all routes.
#include "app/StageApp.h"
#include <fstream>
#include <iostream>
#include <iterator>

using namespace NordDSP;

// Reject DNS-rebinding (Host must be loopback or a literal IP) and
// cross-site requests from browser pages (Origin must match Host).
// Clients that send no Origin (curl, OSC bridges, scripts) are unaffected.
void StageApp::installHttpGuard() {
    svr.set_pre_routing_handler([this](const httplib::Request& req,httplib::Response& res){
        auto hostOnly=[](std::string h){
            if(!h.empty()&&h.front()=='['){const auto e=h.find(']');return e==std::string::npos?h:h.substr(1,e-1);}
            const auto c=h.find(':');return c==std::string::npos?h:h.substr(0,c);
        };
        auto isLoopback=[](const std::string& h){return h=="localhost"||h=="127.0.0.1"||h=="::1";};
        auto isIpLiteral=[](const std::string& h){
            return !h.empty()&&h.find_first_not_of("0123456789.:abcdefABCDEF")==std::string::npos&&
                   (h.find('.')!=std::string::npos||h.find(':')!=std::string::npos);
        };
        auto deny=[&res](){
            res.status=403;
            res.set_content(R"({"ok":false,"error":"forbidden host/origin"})","application/json");
            return httplib::Server::HandlerResponse::Handled;
        };
        const auto rawHost=req.get_header_value("Host");
        const auto host=hostOnly(rawHost);
        if(!isLoopback(host)&&!(httpLanAccess&&isIpLiteral(host)))return deny();
        if(req.has_header("Origin")){
            const auto origin=req.get_header_value("Origin");
            if(origin!="http://"+rawHost)return deny();
        }
        // Page routes of the React console (ui/app). Handled here because
        // httplib serves the "/" file mount before regular GET handlers.
        if((req.method=="GET"||req.method=="HEAD")&&isAppPage(req.path)&&serveAppIndex(res))
            return httplib::Server::HandlerResponse::Handled;
        return httplib::Server::HandlerResponse::Unhandled;
    });
}

// Client-side routes of the React app (react-router). Unknown paths are left to
// the file mounts and API handlers.
bool StageApp::isAppPage(const std::string& path) {
    static const char* const pages[]={"/","/piano","/synth","/organ","/fx","/zones","/library","/library-manager","/settings"};
    for(const char* page:pages)if(path==page)return true;
    return false;
}

// Serves ui/app/index.html (built by webui/). No legacy HTML fallback.
bool StageApp::serveAppIndex(httplib::Response& res) {
    std::ifstream f(findUiFile("app/index.html"), std::ios::binary);
    if(!f)return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    res.set_content(content, "text/html; charset=utf-8");
    res.set_header("Cache-Control", "no-store, max-age=0");
    return true;
}

void StageApp::registerPageRoutes() {
    // Only the compiled React frontend is published; no duplicate HTML interface.
    uiRootDir = findUiDir();
    const auto reactAppDir = (fs::path(uiRootDir) / "app").string();
    if (!svr.set_mount_point("/app", reactAppDir))
        std::cerr << "[WEBUI] No se encontro ui/app; compile webui antes de iniciar.\n";

    // Deep links are served by isAppPage() in the HTTP pre-routing handler.
    // Old bookmarks go to the equivalent React view without publishing old assets.
    const auto redirect = [](const std::string& target) {
        return [target](const httplib::Request&, httplib::Response& res) {
            res.set_redirect(target, 308);
        };
    };
    svr.Get("/legacy", redirect("/"));
    svr.Get("/index.html", redirect("/"));
    svr.Get("/synth.html", redirect("/synth"));
    svr.Get("/organ.html", redirect("/organ"));
    svr.Get("/settings.html", redirect("/settings"));
    svr.Get("/library-manager.html", redirect("/library"));
}
