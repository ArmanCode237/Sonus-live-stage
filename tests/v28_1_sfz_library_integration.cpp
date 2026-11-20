// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
#include "../src/loaders/InstrumentPool.h"
#include "../src/loaders/SampleLibraryManager.h"
#include "../src/loaders/DecodedSampleCache.h"
#include "../src/loaders/BankLoader.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace SonusDSP;
namespace fs=std::filesystem;
static void wav(const fs::path& p){
    std::ofstream f(p,std::ios::binary);assert(f);
    const uint32_t frames=4096, bytes=frames*2, riff=bytes+36, rate=44100, bitrate=rate*2;
    const uint16_t pcm=1,chan=1,align=2,bits=16;
    auto put=[&](const auto& x){f.write(reinterpret_cast<const char*>(&x),sizeof(x));};
    f.write("RIFF",4);put(riff);f.write("WAVEfmt ",8);const uint32_t fmt=16;put(fmt);
    put(pcm);put(chan);put(rate);put(bitrate);put(align);put(bits);
    f.write("data",4);put(bytes);
    for(uint32_t n=0;n<frames;++n){const int16_t v=int16_t((int(n)%101-50)*200);put(v);}
}
int main(){
    const auto tmp=fs::temp_directory_path()/("stage_library_test_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmp/"samples");fs::create_directories(tmp/"cache");
    DecodedSampleCache::setDirectory(tmp/"cache");BankLoader::setNpnoCacheDirectory(tmp/"cache");
    const auto wavfile=tmp/"samples"/"C4.wav";
    const auto source=tmp/"samples"/"simple.sfz";
    wav(wavfile);
    {std::ofstream f(source);f<<"<region> sample=C4.wav key=60 loop_mode=loop_continuous loop_start=256 loop_end=512\n";}
    std::vector<int16_t> reference;
    {
        InstrumentPool pool;std::string report;
        auto bank=pool.getOrLoad(source.string(),report);
        if(!bank){std::cerr<<report<<"\n";return 1;}
        assert(bank->strokes.size()==1&&bank->strokes[0].frames==4096);
        reference=bank->strokes[0].pcmL;
        auto another=pool.getOrLoad(source.string(),report);
        assert(another==bank);
        assert(!reference.empty());
        for(int i=0;i<200&&!DecodedSampleCache::containsFresh(source.string());++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(DecodedSampleCache::containsFresh(source.string()));
    }
    {
        InstrumentPool pool;std::string report;
        auto bank=pool.getOrLoad(source.string(),report);
        assert(bank&&report.find("Desde caché SSD PCM16:")==0);
        assert(bank->strokes[0].pcmL==reference);
        assert(bank->strokes[0].loop.hasLoop);
    }
    {SampleLibraryManager manager;
      assert(manager.prepareAsync({source.string(),source.string()}));
      for(int i=0;i<200&&manager.progress().running;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
      auto status=manager.progress();
      assert(!status.running&&status.total==1&&status.done==1&&status.skipped==1&&status.failed==0);
    }
    // Editing a dependency causes a re-decode, never a stale cached response.
    wav(wavfile);{std::ofstream f(wavfile,std::ios::app);f<<" ";}
    assert(!DecodedSampleCache::containsFresh(source.string()));
    fs::remove_all(tmp);
    std::cout<<"v28.1 SFZ actual WAV -> cache -> reloaded PCM, background skip, dependency invalidation OK\n";
}
