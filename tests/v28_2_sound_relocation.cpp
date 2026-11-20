// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// v28.2 relocation verification: portable path mapping and cache re-keying.
#include "../src/core/LibraryPathRelocation.h"
#include "../src/loaders/DecodedSampleCache.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using namespace SonusDSP;
int main(){
    const auto root=fs::temp_directory_path()/("nvs_v282_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto oldRoot=root/"old library", newRoot=root/"new library";
    fs::create_directories(oldRoot/"Brass");fs::create_directories(root/"cache");
    DecodedSampleCache::setDirectory(root/"cache");
    const auto oldFile=oldRoot/"Brass"/"lead.nsmp3";
    {std::ofstream out(oldFile,std::ios::binary);out<<"synthetic-source-unchanged";}
    PianoInstrument inst;inst.name="Test lead";inst.zoneCount=1;
    StrokeSample s;s.frames=2048;s.sampleRate=35002;s.sampleScale=1.f/32768.f;
    s.pcmL.assign(2048,245);s.pcmR.assign(2048,-137);s.readyFrames->store(2048);
    inst.strokes.push_back(std::move(s));
    assert(DecodedSampleCache::store(oldFile.string(),-1,inst));
    fs::rename(oldRoot,newRoot);
    const auto newFile=newRoot/"Brass"/"lead.nsmp3";
    assert(!DecodedSampleCache::containsFresh(newFile.string()));
    std::string resolved;
    assert(relocateSourcePath(oldFile.string(),oldRoot,newRoot,resolved));
    assert(resolved==newFile.generic_string());
    assert(!relocateSourcePath((root/"old library malicious"/"x.nsmp3").string(),oldRoot,newRoot,resolved));
    assert(DecodedSampleCache::rebindRelocated(oldFile.string(),newFile.string()));
    assert(DecodedSampleCache::containsFresh(newFile.string()));
    PianoInstrument loaded;
    assert(DecodedSampleCache::load(newFile.string(),-1,loaded));
    assert(loaded.strokes.size()==1&&loaded.strokes[0].pcmL==inst.strokes[0].pcmL);
    assert(loaded.strokes[0].pcmR==inst.strokes[0].pcmR);
    assert(loaded.sourcePath==newFile.generic_string());
    // Different source bytes invalidate the relocated cache, no stale PCM leaks.
    {std::ofstream out(newFile,std::ios::binary|std::ios::app);out<<"changed";}
    assert(!DecodedSampleCache::containsFresh(newFile.string()));
    assert(!DecodedSampleCache::load(newFile.string(),-1,loaded));
    // SFZ cache is always re-prepared because its dependent WAV paths moved too.
    const auto sfz=newRoot/"Brass"/"lead.sfz";
    {std::ofstream out(sfz);out<<"<region> sample=lead.wav";}
    assert(!DecodedSampleCache::rebindRelocated((oldRoot/"Brass"/"lead.sfz").string(),sfz.string()));
    fs::remove_all(root);
    std::cout<<"v28.2 path-aware relocation, PCM16 reuse and invalidation OK\n";
}
