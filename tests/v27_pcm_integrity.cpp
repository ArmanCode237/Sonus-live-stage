#include "../src/loaders/BankLoader.h"
#include <cstdint>
#include <iostream>
#include <string>
using namespace SonusDSP;
int main(int argc,char**argv){
 if(argc<2)return 2;
 PianoInstrument bank;std::string status;
 if(!BankLoader::loadNpnoNative(argv[1],bank,status)){std::cerr<<status;return 1;}
 uint64_t checksum=14695981039346656037ull;
 const auto mix=[&](uint32_t v) {checksum^=v;checksum*=1099511628211ULL;};
 for(const auto& s:bank.strokes){
  mix(s.frames);mix(s.loop.start);mix(s.loop.length);
  for(const auto x:s.pcmL)mix(uint16_t(x));
  for(const auto x:s.pcmR)mix(uint16_t(x));
 }
 std::cout<<"PCM_CHECKSUM="<<std::hex<<checksum<<" samples="<<std::dec<<bank.strokes.size()<<"\n";
}
