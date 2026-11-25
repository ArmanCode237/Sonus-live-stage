#include "../src/loaders/Sf2Loader.h"
#include "../src/modules/StagePianoEngine.h"
#include <iostream>
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>
using namespace SonusDSP;
int main(int argc,char**argv){
 if(argc<2)return 8;
 std::string msg;PianoInstrument instrument;
 if(!Sf2Loader::load(argv[1],instrument,msg,1)){std::cerr<<msg;return 1;}
 auto shared=std::make_shared<PianoInstrument>(std::move(instrument));
 StagePianoEngine e;e.setSampleRate(48000);e.setPianoEnabled(true);
 e.multiSynth_.synthEnabled=false;e.organModule_.organEnabled=false;
 for(int i=0;i<5;++i){e.setLayerInstrument(i,shared);e.setPianoLayerEnabled(i,i==0);}
 e.setPianoLayerSampleParam(0,0,.005f);
 e.noteOn(60,110);
 std::vector<float> audio(4096*2);double energy=0;
 for(int n=0;n<8;n++){e.render(audio.data(),4096);for(auto x:audio){if(!std::isfinite(x))return 2;energy+=double(x)*x;}}
 std::cout<<"SF2 audio energy "<<energy<<"\n";
 if(energy<.00001)return 3;
 e.setModulePan(0,-1);e.noteOn(60,110);
 double left=0,right=0;
 for(int n=0;n<8;n++){e.render(audio.data(),4096);for(int k=0;k<4096;k++){left+=std::abs(audio[k*2]);right+=std::abs(audio[k*2+1]);}}
 if(left<=right*1.2){std::cerr<<"Pan failed "<<left<<"/"<<right;return 4;}
 auto current=e.getCurrentParams();if(std::abs(current.pianoPan+1)>1e-4)return 5;
 e.setModuleTune(0,40);if(std::abs(e.getCurrentParams().pianoTune-40)>1e-4)return 6;
 std::cout<<"PASS SF2 playback, module pan and master tuning\n";
}
