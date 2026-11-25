// Sonus Live Stage standalone entry point. The application itself lives in
// src/app/StageApp.{h,cpp}; HTTP routes are grouped in src/app/routes/.
#include "app/StageApp.h"
#include "app/LiveLog.h"
#include "app/UserDataPaths.h"
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
#if !defined(__APPLE__)
    if(fs::is_directory("ui")||fs::is_directory("../ui"))return;
#endif
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
#if defined(__APPLE__)
    // .app/Contents/MacOS/<binary> -> .app/Contents/Resources/{ui,presets,config}
    if (!executable.empty()) {
        const auto resources = executable.parent_path().parent_path()/"Resources";
        if (fs::is_directory(resources/"ui")) setStageAssetRoot(resources.string());
    }
#endif
    if(!executable.empty()&&fs::is_directory(executable.parent_path()/"ui")){
        std::error_code ec;fs::current_path(executable.parent_path(),ec);
    }
}

// v32.4 stage safety (Windows): a click in the console window must never freeze the app
// (QuickEdit suspends every thread that writes to it), and the PC must not sleep or blank
// the screen mid-show: MIDI playing does not count as user activity for Windows.
static void prepareForStage() {
#if defined(_WIN32)
    if (HANDLE in = GetStdHandle(STD_INPUT_HANDLE); in && in != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(in, &mode)) SetConsoleMode(in, (mode & ~DWORD(ENABLE_QUICK_EDIT_MODE)) | ENABLE_EXTENDED_FLAGS);
    }
    // Held by the main thread (it lives for the whole session, inside svr.listen()).
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif
}

// v32.4 window application: no console of its own. "--console" opens one (debugging);
// redirected output (tests, scripts) keeps working because the handles are inherited.
static void openConsoleIfAsked(int argc, char* argv[]) {
#if defined(_WIN32) && defined(STAGE_NATIVE_WINDOW)
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) != "--console") continue;
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio();
        return;
    }
#else
    (void)argc; (void)argv;
#endif
}

int main(int argc, char* argv[]) {
    enterInstallDirectoryIfNeeded();
    openConsoleIfAsked(argc, argv);
    prepareForStage();
    live_log::install(stageUserDataDir() / "logs");
    // The file keeps the last completed step even if an external driver
    // crashes natively (which catch(...) cannot intercept).
    { std::ofstream trace("startup_trace.log", std::ios::trunc); trace << "Sonus Live Stage - diagnostico de arranque\n"; }
    bootStep("Iniciando constructor DSP");
    std::cout << "=========================================================\n";
    std::cout << "         SONUS LIVE STAGE - STAGE WORKSTATION          \n";
    std::cout << "=========================================================\n\n";

    StageAppOptions options;
    options.makeBackend = makeRtAudioBackend;      // v33: own audio / MIDI devices (RtAudio / RtMidi)
    StageApp app(argc, argv, std::move(options));
    const int code = app.run();
#if defined(_WIN32)
    SetThreadExecutionState(ES_CONTINUOUS);
#endif
    return code;
}

#if defined(_WIN32) && defined(STAGE_NATIVE_WINDOW)
// GUI subsystem entry point (WIN32_EXECUTABLE): same arguments as main().
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif
