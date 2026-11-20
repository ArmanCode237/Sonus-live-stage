#include "../src/loaders/BankLoader.h"
#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
using namespace SonusDSP;
int main(int argc,char**argv){
 if(argc<2){std::cerr<<"usage: bench file.npno\n";return 2;}
 for(int i=0;i<2;++i){
  PianoInstrument p;std::string status;auto start=std::chrono::steady_clock::now();
  bool ok=BankLoader::loadNpnoNative(argv[1],p,status);
  auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
  uint64_t frames=0, bytes=0;int ready=0;for(auto& s:p.strokes){frames+=s.frames;bytes+=(s.pcmL.size()+s.pcmR.size())*2;ready+=s.readyFrames && s.readyFrames->load()==s.frames;}
  std::cout<<"RUN "<<i<<" ok="<<ok<<" ms="<<ms<<" strokes="<<p.strokes.size()<<" frames="<<frames<<" pcmBytes="<<bytes<<" ready="<<ready<<" status="<<status<<"\n";
  if(!ok)return 1;
 }
}
