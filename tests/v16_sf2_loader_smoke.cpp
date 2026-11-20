#include "../src/loaders/Sf2Loader.h"
#include <iostream>
#include <cmath>
int main(int argc,char**argv){
 if(argc<2)return 8;std::string status;SonusDSP::PianoInstrument inst;
 auto items=SonusDSP::Sf2Loader::listPresets(argv[1]);
 if(items.size()!=2){std::cerr<<"Expected two presets, got "<<items.size();return 1;}
 if(!SonusDSP::Sf2Loader::load(argv[1],inst,status,1)){std::cerr<<status;return 2;}
 if(inst.strokes.empty()||inst.strokes[0].frames<16||inst.sf2PresetIndex!=1){std::cerr<<"Bad instrument";return 3;}
 for(const auto& st:inst.strokes)for(int16_t x:st.pcmL)if(x<-32768 || x>32767)return 4;
 std::cout<<"PASS SF2: "<<inst.name<<" ("<<inst.zoneCount<<" region), index "<<inst.sf2PresetIndex<<"\n";
 SonusDSP::PianoInstrument reject;
 if(SonusDSP::Sf2Loader::load("/tmp/no_such_file_123.sf2",reject,status)){std::cerr<<"bad file accepted";return 5;}
}
