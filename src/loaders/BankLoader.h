#pragma once
#include "NordTypes.h"
#include <string>
#include <vector>


namespace NordDSP {

class BankLoader {
public:
    static bool loadAny(const std::string& pathStr, PianoInstrument& outInstrument, std::string& outStatusMsg);
    static bool loadNpnoNative(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg);
    static bool loadNsmpFallback(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg);
    static bool loadFromDirectory(const std::string& folderPath, PianoInstrument& outInstrument);
    static bool loadFromSfz(const std::string& sfzFilePath, PianoInstrument& outInstrument);
    static bool loadClaviaProgramPreset(const std::string& filePath, PianoInstrument& ioInstrument, std::string& outStatusMsg);
    static ClaviaHeaderInfo inspectClaviaHeader(const std::string& filePath);
    static std::vector<LibraryEntry> scanLibraryDirectory(const std::string& rootDir);
    static bool loadNsmpNative(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg);

private:
    static void ensureSustainLoop(StrokeSample& s);
    static bool loadWaveFile(const std::string& wavPath, StrokeSample& s);
    static void loadWavesInParallel(std::vector<StrokeSample>& strokes, const std::vector<std::string>& wavPaths);
};

} // namespace NordDSP