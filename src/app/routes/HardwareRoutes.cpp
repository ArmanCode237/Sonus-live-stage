// Generated from the former monolithic main.cpp: route bodies are unchanged.
#include "app/routes/RouteCommon.h"

using namespace NordDSP;

void StageApp::registerHardwareRoutes() {
    svr.Get("/api/hardware", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(hw->getHardwareStatusJson().dump(), "application/json");
    });

    svr.Post("/api/hardware/rescan", [this](const httplib::Request&, httplib::Response& res) {
        hw->scanAllAudioDevices();
        hw->restoreOrInitAudio();
        hw->refreshMidiPorts();
        auto st = hw->getHardwareStatusJson();
        st["message"] = "Hardware re-escaneado y configuracion verificada.";
        res.set_content(st.dump(), "application/json");
    });

    svr.Post("/api/hardware/audio", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        int uid = body.value("uid", -1);
        unsigned int sr = body.value("sampleRate", 0u);
        unsigned int bufSize = body.value("bufferSize", 128u);
        std::string msg;
        bool ok = hw->configureAudio(uid, sr, bufSize, msg);
        auto st = hw->getHardwareStatusJson();
        st["ok"] = ok;
        st["message"] = msg;
        res.set_content(st.dump(), "application/json");
    });

    svr.Post("/api/hardware/midi", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (body.value("refresh", false)) {
            hw->refreshMidiPorts();
        } else if (body.contains("forgetName")) {
            hw->forgetDisconnectedMidiPort(body.value("forgetName", ""));
        } else {
            std::string portName = body.value("name", "");
            bool enabled = body.value("enabled", false);
            if (!portName.empty()) {
                hw->setMidiPortEnabledByName(portName, enabled);
            } else if (body.contains("index")) {
                hw->setMidiPortEnabledByIndex(body.value("index", -1), enabled);
            }
        }
        res.set_content(hw->getHardwareStatusJson().dump(), "application/json");
    });
}
