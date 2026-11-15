// Optional real-file validation: no proprietary recordings are included.
#include "../src/loaders/BankLoader.h"
#include "../src/loaders/SampleLibraryManager.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <thread>
using namespace NordDSP;
namespace fs=std::filesystem;
int main(int argc,char** argv){
 if(argc<3){std::cerr<<"Uso: v28_npno_cache_location archivo.npno /ruta/cache\n";return 2;}
 const fs::path location=argv[2];std::error_code ec;fs::create_directories(location,ec);
 if(ec)return 3;
 BankLoader::setNpnoCacheDirectory(location);
 SampleLibraryManager library;
 if(!library.prepareAsync({argv[1]}))return 4;
 for(int attempt=0;attempt<2000&&library.progress().running;++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
 const auto state=library.progress();
 if(state.running||state.done!=1||state.failed||!BankLoader::hasNpnoCache(argv[1])){
    std::cerr<<"Error al preparar SSD: "<<state.error<<"\n";return 5;
 }
 PianoInstrument bank;std::string report;
 if(!BankLoader::loadNpnoNative(argv[1],bank,report,true))return 6;
 for(const auto& stroke:bank.strokes)if(!stroke.readyFrames||stroke.readyFrames->load()!=stroke.frames)return 7;
 std::cout<<"READY: "<<bank.name<<", strokes="<<bank.strokes.size()<<", "<<report<<"\n";
 return 0;
}
