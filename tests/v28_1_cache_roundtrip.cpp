// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
#include "../src/loaders/DecodedSampleCache.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <chrono>
namespace fs=std::filesystem;
using namespace SonusDSP;
int main(){
    auto tmp=fs::temp_directory_path()/ ("nvs_v281_test_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmp/"source");fs::create_directories(tmp/"pcm");
    DecodedSampleCache::setDirectory(tmp/"pcm");
    const auto path=tmp/"source/test.nsmp3";
    {std::ofstream o(path,std::ios::binary);o<<"fake format for roundtrip metadata test";}
    PianoInstrument original;
    original.name="Cache test";original.variant="str";original.formatType="NSMP3";
    original.zoneCount=1;original.memoryMB=0.25;
    original.sampleUnison.available=true;original.sampleUnison.detuneRaw[0]=23;
    original.sampleVibrato.available=true;original.sampleVibrato.raw[4]=44;
    original.repeatAlternateKeys[64][0]=5;original.repeatAlternateCounts[64]=1;
    StrokeSample s;s.index=11;s.sampleRate=35002;s.frames=8192;s.sampleScale=1.f/32768.f;
    s.lokey=2;s.hikey=84;s.lovel=10;s.hivel=95;
    s.flags=1;s.alternateStartFrame=210;
    s.loop={true,false,2000,256,2256};s.decayCoef=1.0f;
    s.pcmL.resize(s.frames);s.pcmR.resize(s.frames);
    for(uint32_t i=0;i<s.frames;++i){s.pcmL[i]=int16_t((i*7)%32767);s.pcmR[i]=int16_t(1234-(i*5)%2345);}
    s.readyFrames->store(s.frames);
    original.strokes.push_back(std::move(s));
    assert(DecodedSampleCache::store(path.string(),-1,original));
    assert(DecodedSampleCache::containsFresh(path.string()));
    PianoInstrument restored;
    assert(DecodedSampleCache::load(path.string(),-1,restored));
    assert(restored.name==original.name && restored.zoneCount==1 && restored.strokes.size()==1);
    assert(restored.sampleUnison.detuneRaw[0]==23 && restored.sampleVibrato.raw[4]==44);
    assert(restored.repeatAlternateCounts[64]==1 && restored.repeatAlternateKeys[64][0]==5);
    const auto& a=restored.strokes[0];const auto& b=original.strokes[0];
    assert(a.loop.start==2000&&a.loop.end==2256&&a.decayCoef==1.0f);
    assert(a.pcmL==b.pcmL&&a.pcmR==b.pcmR&&a.sampleScale==b.sampleScale);
    assert(a.readyFrames->load()==a.frames&&a.alternateStartFrame==210);
    assert(!DecodedSampleCache::containsFresh(path.string(),5));
    assert(!DecodedSampleCache::load(path.string(),5,restored));
    // A corrupted but date-matching file must not publish any PCM.
    const auto cached=DecodedSampleCache::cachePath(path.string());
    {std::fstream f(cached,std::ios::binary|std::ios::in|std::ios::out);assert(f);
      f.seekg(-1,std::ios::end);char bad=0;f.read(&bad,1);
      bad^=0x7f;f.seekp(-1,std::ios::end);f.write(&bad,1);}
    PianoInstrument corrupted;
    assert(!DecodedSampleCache::load(path.string(),-1,corrupted));
    assert(corrupted.strokes.empty());
    assert(DecodedSampleCache::store(path.string(),-1,original));
    {std::ofstream f(path,std::ios::app);f<<"changed";}
    assert(!DecodedSampleCache::containsFresh(path.string()));
    assert(!DecodedSampleCache::load(path.string(),-1,corrupted));
    // Dependencies: changing a WAV in the SFZ folder invalidates its PCM cache.
    auto sfz=tmp/"source/test.sfz";auto wav=tmp/"source/a.wav";
    {std::ofstream f(sfz);f<<"<region> sample=a.wav key=60";}
    {std::ofstream f(wav);f<<"placeholder WAV bytes";}
    assert(DecodedSampleCache::store(sfz.string(),-1,original));
    assert(DecodedSampleCache::containsFresh(sfz.string()));
    {std::ofstream f(wav,std::ios::app);f<<"extra sample";}
    assert(!DecodedSampleCache::containsFresh(sfz.string()));
    fs::remove_all(tmp);
    std::cout<<"v28.1 cache metadata/PCM integrity, corruption, SF2 identity and SFZ dependencies OK\n";
}
