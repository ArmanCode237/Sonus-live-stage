#pragma once
#include "../core/NordTypes.h"
#include <string>
#include <vector>

namespace NordDSP {
// Importa muestras PCM16 de SoundFont 2.x; selector preset opcional (índice phdr).
class Sf2Loader {
public:
    static bool load(const std::string& file, PianoInstrument& out, std::string& error, int presetIndex=-1);
    static std::vector<std::string> listPresets(const std::string& file);
};
}
