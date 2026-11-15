// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// Generated NPno: factory loop >=4 frames, alternate stroke, key-up stroke,
// and NPCACHE5 invalidation without proprietary recordings.
#include "../src/loaders/BankLoader.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <chrono>
#include <iostream>
#include <vector>
using namespace NordDSP;
namespace fs=std::filesystem;
static void be16(std::vector<uint8_t>& b,size_t i,uint16_t n){b[i]=uint8_t(n>>8);b[i+1]=uint8_t(n);}
static void be32(std::vector<uint8_t>& b,size_t i,uint32_t n){be16(b,i,uint16_t(n>>16));be16(b,i+2,uint16_t(n));}
int main(){
    const fs::path old=fs::current_path();
    const auto now=std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir=fs::temp_directory_path()/("stage_npno24_"+std::to_string(now));
    fs::create_directories(dir);fs::current_path(dir);
    constexpr size_t base=0x900,words=511,fpb=446;
    constexpr uint32_t blocks[3]={20,15,4};
    const size_t totalBlocks=blocks[0]+blocks[1]+blocks[2];
    std::vector<uint8_t> bytes(base+totalBlocks*words*2,0);
    bytes[0]='C';bytes[1]='N';bytes[2]='S';bytes[3]='P';
    be16(bytes,0x61c,0x464);be16(bytes,0x61e,1);be16(bytes,0x620,3);
    bytes[0x8c+69]=65;be16(bytes,0x622+65*2,3);
    uint32_t total=0;
    for(int index=0;index<3;++index){
        const size_t rec=0x732+index*0x76;
        const uint32_t offset=uint32_t(base+total*words*2);
        be32(bytes,rec,offset);
        bytes[rec+4]=uint8_t(index);
        be32(bytes,rec+6,blocks[index]*fpb);
        be16(bytes,rec+0x0a,uint16_t(blocks[index]));
        be16(bytes,rec+0x34,125);
        bytes[rec+0x72]=44;bytes[rec+0x73]=50;
        if(index<2){be32(bytes,rec+0x1c,2500);be32(bytes,rec+0x20,4);be32(bytes,rec+0x2e,0x800000);}
        for(uint32_t block=0;block<blocks[index];++block){
            const size_t at=offset+block*words*2;
            be16(bytes,at,16);
            for(int i=0;i<510;++i){
                const double phase=2.*3.141592653589793*220.*(block*fpb+i)/44100.;
                const int16_t x=int16_t(std::lround(8000.*std::sin(phase)));
                be16(bytes,at+2+i*2,uint16_t(x));
            }
        }
        total+=blocks[index];
    }
    {std::ofstream f("factory.npno",std::ios::binary);
     f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));}
    PianoInstrument first;std::string status;
    if(!BankLoader::loadNpnoNative("factory.npno",first,status)){
        std::cerr<<status<<"\n";return 1;
    }
    assert(first.strokes.size()==3);
    const auto& a=first.strokes[0],&b=first.strokes[1],&c=first.strokes[2];
    assert(a.loop.hasLoop && a.loop.start==2500 && a.loop.length==4);
    assert(a.decayCoef==1.f && a.flags==0);
    assert(b.flags==1 && !b.isReleaseStroke);
    assert(c.isReleaseStroke && c.flags==2 && !c.loop.hasLoop);
    assert(a.sampleScale>0 && a.pcmL.size()==a.frames);
    PianoInstrument second;
    if(!BankLoader::loadNpnoNative("factory.npno",second,status))return 2;
    assert(status.find("caché")!=std::string::npos);
    assert(second.strokes[0].loop.length==4);
    assert(second.strokes[2].isReleaseStroke);
    bool found=false;
    for(const auto& e:fs::directory_iterator(dir/".cache"))if(e.path().extension()==".npcache"){
        std::ifstream f(e.path(),std::ios::binary);char magic[8]{};
        f.read(magic,8);assert(std::string(magic,8)=="NPCACHE5");found=true;
    }
    assert(found);
    fs::current_path(old);fs::remove_all(dir);
    std::cout<<"PASS NPno 4-frame factory loop + alternates + release + NPCACHE5\n";
}
