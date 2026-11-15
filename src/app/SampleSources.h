#pragma once
// Sample-source persistence shared by programs, Sounds and performances.
#include "app/AppCommon.h"
#include "loaders/InstrumentPool.h"
#include "modules/NordPianoEngine.h"
#include <mutex>
#include <utility>
#include <vector>

// Shared PCM16 pool for the eight global program slots and five piano layers.
extern InstrumentPool instPool;
// Stable source aliases recorded when a user moves a library folder.
// Only HTTP/control threads access this table; never the audio callback.
extern std::mutex relocatedPathsMutex;
extern std::vector<std::pair<fs::path,fs::path>> relocatedPaths;

// Never replace an existing file with a partial/truncated JSON document.
bool saveJsonAtomic(const fs::path& destination,const json& value);
fs::path resolveSavedSamplePath(const fs::path& requested);
json serializePianoLayerSources(const NordPianoEngine& engine);
bool restorePianoLayerSources(const json& program, NordPianoEngine& engine,
                              const std::string& libraryRoot, std::string& statusMsg);
