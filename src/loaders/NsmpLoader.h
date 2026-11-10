#pragma once
#include "Nordtypes.h"
#include <string>

namespace NordDSP {

class NsmpLoader {
public:
    static bool loadNsmpNative(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg);
    static bool loadNsmpFallback(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg);
};

} // namespace NordDSP