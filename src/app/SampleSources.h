#pragma once
// Sample-source persistence shared by programs, Sounds and performances.
#include "app/AppCommon.h"
#include "loaders/InstrumentPool.h"
#include "modules/StagePianoEngine.h"
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
// v34: folders where a saved sample is looked for when its stored path no longer exists
// (programs created on another computer, Windows paths, "./samples/..." relative paths).
// Control threads only. Replacing the list invalidates the filename index.
void setSampleSearchRoots(const std::vector<fs::path>& roots);
// Finds `saved` inside the search roots: same relative tail first, then by file name
// (case-insensitive, recursive). Empty path when not found.
fs::path findSampleInSearchRoots(const std::string& saved);
json serializePianoLayerSources(const StagePianoEngine& engine);
bool restorePianoLayerSources(const json& program, StagePianoEngine& engine,
                              const std::string& libraryRoot, std::string& statusMsg);
