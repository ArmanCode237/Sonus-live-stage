#pragma once
#include <filesystem>
#include <string>

// Per-user writable storage: Program Files and app bundles are not writable.
std::filesystem::path stageUserDataDir();
// User copy of a bundled file (copied from the working dir on first use).
std::string stageUserFile(const char* name);
// presets/<module>/user/presets.json inside the user data dir.
std::string stageUserPresetFile(const char* module);
// Locate shipped UI assets even when launched from build/Release.
std::string findUiDir();
std::string findUiFile(const std::string& filename);
