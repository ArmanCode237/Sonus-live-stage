#include "../src/loaders/Sf2Loader.h"
#include "../src/modules/NordPianoEngine.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>
using namespace NordDSP;
static double tailEnergy(const std::shared_ptr<const PianoInstrument>& bank,float releaseSec){
 NordPianoEngine e;e.setSampleRate(48000);e.setPianoEnabled(true);e.multiSynth_.synthEnabled=false;e.organModule_.organEnabled=false;
 for(int i=1;i<5;i++)e.setPianoLayerEnabled(i,false);
 e.setPianoLayerSampleParam(0,0,.005f);e.setPianoLayerSampleParam(0,1,.1f);e.setPianoLayerSampleParam(0,2,1.f);
 e.setPianoLayerSampleParam(0,3,releaseSec);e.setPianoLayerSampleParam(0,4,1.f);
 e.setLayerInstrument(0,bank);std::vector<float> buf(512*2);
 e.noteOn(60,103);for(int i=0;i<12;i++)e.render(buf.data(),512);
 e.noteOff(60);double energy=0;
 for(int i=0;i<90;i++){e.render(buf.data(),512);if(i>60)for(float s:buf){if(!std::isfinite(s))return -1;energy+=double(s)*s;}}
 return energy;
}
int main(int argc,char** argv){if(argc<2)return 10;PianoInstrument ins;std::string error;
 if(!Sf2Loader::load(argv[1],ins,error,0)){std::cerr<<error;return 2;}
 auto bank=std::make_shared<PianoInstrument>(std::move(ins));
 const double fast=tailEnergy(bank,.04f),slow=tailEnergy(bank,2.f);
 std::cout<<"SF2 short-release energy="<<fast<<" long-release energy="<<slow<<"\n";
 if(fast<0||slow<1e-8||!(slow>fast*10)){std::cerr<<"SF2 release envelope unresponsive\n";return 3;}
 std::cout<<"PASS SF2 ENV release follows layer control\n";
}
