// Optional commercial-file validation. Usage: test <source.npno>
#include "../src/loaders/BankLoader.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
namespace fs=std::filesystem;
using namespace NordDSP;
uint64_t checksum(const PianoInstrument& p){
 uint64_t h=14695981039346656037ULL;
 auto mix=[&](const std::vector<int16_t>& v){for(const auto& x:v){const uint16_t n=static_cast<uint16_t>(x);h^=(n&255);h*=1099511628211ULL;h^=(n>>8);h*=1099511628211ULL;}};
 for(const auto& s:p.strokes){mix(s.pcmL);mix(s.pcmR);}
 return h;
}
int main(int argc,char** argv){
 if(argc<2)return 2;
 const auto root=fs::temp_directory_path()/("v282_npno_real_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 const auto oldRoot=root/"old",newRoot=root/"new",cache=root/"cache";
 fs::create_directories(oldRoot);fs::create_directories(cache);
 const auto oldFile=oldRoot/"grand.npno";fs::copy_file(argv[1],oldFile);
 BankLoader::setNpnoCacheDirectory(cache);
 PianoInstrument before;std::string msg;
 if(!BankLoader::loadNpnoNative(oldFile.string(),before,msg,false))return 3;
 if(!BankLoader::hasNpnoCache(oldFile.string()))return 4;
 const auto beforeHash=checksum(before);
 fs::rename(oldRoot,newRoot);
 const auto newFile=newRoot/"grand.npno";
 if(!BankLoader::rebindNpnoCache(oldFile.string(),newFile.string()))return 5;
 if(!BankLoader::hasNpnoCache(newFile.string()))return 6;
 PianoInstrument after;
 if(!BankLoader::loadNpnoNative(newFile.string(),after,msg,true))return 7;
 if(after.strokes.size()!=before.strokes.size()||beforeHash!=checksum(after))return 8;
 std::cout<<"REAL NPNO PCM16 RELOCATION PASS strokes="<<after.strokes.size()<<" checksum="<<std::hex<<beforeHash<<"\n";
 fs::remove_all(root);
}
