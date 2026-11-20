#pragma once
#include "StageTypes.h"
#include <string>

namespace SonusDSP {

class NsmpLoader {
public:
    static bool loadNsmpNative(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg);
    static bool loadNsmpFallback(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg);
};

} // namespace SonusDSP