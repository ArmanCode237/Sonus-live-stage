// Live Stage standalone entry point. The application itself lives in
// src/app/StageApp.{h,cpp}; HTTP routes are grouped in src/app/routes/.
#include "app/StageApp.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

// Installed app may be started from a DAW shortcut, desktop or terminal.
// Resolve packaged HTML/presets relative to the binary when CWD differs.
static void enterInstallDirectoryIfNeeded() {
    if(fs::is_directory("ui")||fs::is_directory("../ui"))return;
    fs::path executable;
#if defined(_WIN32)
    wchar_t path[32768]{};
    const auto count=GetModuleFileNameW(nullptr,path,32767);
    if(count>0&&count<32767)executable=fs::path(path);
#elif defined(__APPLE__)
    uint32_t capacity=0;_NSGetExecutablePath(nullptr,&capacity);
    std::vector<char> path(capacity+1,0);
    if(_NSGetExecutablePath(path.data(),&capacity)==0)executable=fs::weakly_canonical(path.data());
#else
    std::error_code ec;executable=fs::read_symlink("/proc/self/exe",ec);
#endif
    if(!executable.empty()&&fs::is_directory(executable.parent_path()/"ui")){
        std::error_code ec;fs::current_path(executable.parent_path(),ec);
    }
}

int main(int argc, char* argv[]) {
    enterInstallDirectoryIfNeeded();
    // The file keeps the last completed step even if an external driver
    // crashes natively (which catch(...) cannot intercept).
    { std::ofstream trace("startup_trace.log", std::ios::trunc); trace << "Live Stage 16 - diagnostico de arranque\n"; }
    bootStep("Iniciando constructor DSP");
    std::cout << "=========================================================\n";
    std::cout << "   NORD VIRTUAL STAGE 11.1 - STAGE WORKSTATION EDITION    \n";
    std::cout << "=========================================================\n\n";

    StageApp app(argc, argv);
    return app.run();
}
