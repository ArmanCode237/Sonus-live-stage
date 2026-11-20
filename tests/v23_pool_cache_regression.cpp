// Standalone regression: synthetic CNSP, fully decoded and normalized BEFORE
// the instrument becomes playable; RAM sharing and NPCACHE5 persistence.
// No real copyrighted samples or soundcard needed.
#include "../src/loaders/InstrumentPool.h"
#include "../src/loaders/BankLoader.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
using namespace SonusDSP;
namespace fs=std::filesystem;

static void put16(std::vector<uint8_t>& b,size_t i,uint16_t x){b[i]=uint8_t(x>>8);b[i+1]=uint8_t(x);}
static void put32(std::vector<uint8_t>& b,size_t i,uint32_t x){put16(b,i,uint16_t(x>>16));put16(b,i+2,uint16_t(x));}
static void makeCnsp(const fs::path& path){
    constexpr uint32_t blocks=140, words=511, framesPer=446;
    constexpr size_t offset=0x900;
    std::vector<uint8_t> file(offset+blocks*words*2,0);
    file[0]='C';file[1]='N';file[2]='S';file[3]='P';
    put16(file,0x61c,0x464);put16(file,0x61e,1);put16(file,0x620,1);
    file[0x8c+60]=56;put16(file,0x622+56*2,1);
    const size_t rec=0x732;
    put32(file,rec,offset);file[rec+4]=0;file[rec+5]=0;
    put32(file,rec+6,blocks*framesPer);put16(file,rec+0x0a,blocks);
    put16(file,rec+0x34,125);file[rec+0x72]=44;file[rec+0x73]=50;
    for(uint32_t b=0;b<blocks;++b){
        const size_t at=offset+b*words*2;
        put16(file,at,16); // width=16, order=0: each word is a signed sample.
        for(uint32_t i=0;i<510;++i){
            const double phase=2.*3.141592653589793*220.0*(b*framesPer+i)/44100.;
            const int16_t pcm=int16_t(std::lround((b<6?1000.:22000.)*std::sin(phase)));
            put16(file,at+2+i*2,uint16_t(pcm));
        }
    }
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()),std::streamsize(file.size()));
}

int main(){
    const auto before=fs::current_path();
    const auto now=std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir=fs::temp_directory_path()/("stage_dfdfixture_"+std::to_string(now));
    fs::create_directories(dir);fs::current_path(dir);
    const auto file=dir/"regression.npno";
    makeCnsp(file);
    {
        InstrumentPool pool;
        std::string status;
        auto bank=pool.getOrLoad(file.string(),status);
        if(!bank||bank->strokes.size()!=1){std::cerr<<"First load failed: "<<status<<"\n";return 1;}
        auto& sample=bank->strokes.front();
        const auto initial=sample.readyFrames->load(std::memory_order_acquire);
        if(initial!=sample.frames){std::cerr<<"Published incomplete playable bank: "<<initial<<"/"<<sample.frames<<"\n";return 2;}
        // The late tail is deliberately much louder than the six preload
        // blocks. A scale estimated from the attack alone distorts this bank.
        if(!(sample.sampleScale<1.f/18000.f)){
            std::cerr<<"Normalization still based on attack prefix: "<<sample.sampleScale<<"\n";return 3;
        }
        auto again=pool.getOrLoad(file.string(),status);
        if(again!=bank){std::cerr<<"RAM pool miss\n";return 4;}
        if(sample.pcmL.size()!=sample.frames||sample.pcmR.size()!=sample.frames){return 5;}
        if(sample.sampleScale<=0 || !std::isfinite(sample.sampleScale)){return 6;}
        for(int i=0;i<3000&&!fs::exists(dir/".cache");++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        bool cacheReady=false;
        for(int i=0;i<3000&&!cacheReady;++i){
            for(const auto& e:fs::directory_iterator(dir/".cache"))
                if(e.path().extension()==".npcache")cacheReady=true;
            if(!cacheReady)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(!cacheReady){std::cerr<<"No .npcache produced\n";return 7;}
        std::cout<<"First load: "<<initial<<"/"<<sample.frames<<" ready frames, "
                 <<bank->memoryMB<<" MiB, auto-loop published="
                 <<bool(sample.publishedAutoLoop->load())<<"\n";
    }
    {
        InstrumentPool pool;std::string status;
        auto bank=pool.getOrLoad(file.string(),status);
        if(!bank||status.find("caché")==std::string::npos){
            std::cerr<<"Disk cache was not hit: "<<status<<"\n";return 8;
        }
        if(bank->strokes[0].readyFrames->load()!=bank->strokes[0].frames)return 9;
        std::cout<<"Cold-start binary cache: "<<status<<"\n";
    }
    fs::current_path(before);
    std::error_code ec;fs::remove_all(dir,ec);
    std::cout<<"PASS: complete-on-publish CNSP, late-peak normalization, RAM pool, .npcache\n";
    return 0;
}
