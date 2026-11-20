#pragma once
#include <filesystem>
#include <string>

namespace SonusDSP {
// Control-thread helpers only. Prefixes are path-component aware: renaming
// '/Samples/Piano' never matches '/Samples/Pianos-BACKUP'.
inline bool relocateSourcePath(const std::string& input,
                               const std::filesystem::path& oldRoot,
                               const std::filesystem::path& newRoot,
                               std::string& output) {
    namespace fs = std::filesystem;
    if(input.empty() || !fs::path(input).is_absolute() || !oldRoot.is_absolute() || !newRoot.is_absolute()) return false;
    const fs::path saved = fs::path(input).lexically_normal();
    const fs::path from = oldRoot.lexically_normal();
    const fs::path to = newRoot.lexically_normal();
    const fs::path relative = saved.lexically_relative(from);
    if(relative.empty() || relative == ".") {if(saved != from) return false;}
    else if(relative.is_absolute() || *relative.begin() == "..") return false;
    const fs::path target=(to/relative).lexically_normal();
    std::error_code ec;
    if(fs::is_regular_file(saved,ec)) return false; // an existing original must take precedence
    ec.clear();
    if(!fs::is_regular_file(target,ec) || ec) return false;
    output=target.generic_string();
    return true;
}
}
