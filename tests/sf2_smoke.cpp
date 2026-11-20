#include "../src/loaders/Sf2Loader.h"
#include <iostream>
#include <cmath>
int main(int argc,char**argv){
 if(argc!=2)return 2;
 auto presets=SonusDSP::Sf2Loader::presets(argv[1]);
 if(presets.size()!=1||presets[0].name!="Smoke Bank"||presets[0].bank!=0||presets[0].program!=0)return 3;
 SonusDSP::PianoInstrument instrument;std::string error;
 if(!SonusDSP::Sf2Loader::load(argv[1],0,0,instrument,error)){std::cerr<<error<<'\n';return 4;}
 if(instrument.strokes.size()!=1||instrument.strokes[0].frames!=512)return 5;
 float maxAmplitude=0;
 for(float x:instrument.strokes[0].pcmL)maxAmplitude=std::max(maxAmplitude,std::fabs(x/32768.f));
 if(maxAmplitude<0.3f)return 6;
 std::cout<<"SF2 PASS: "<<presets[0].name<<", frames="<<instrument.strokes[0].frames<<"\n";
}
