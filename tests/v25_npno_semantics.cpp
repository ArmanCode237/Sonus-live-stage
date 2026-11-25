// NPno semantic regression: attack vs pedal-resonance, velocity bank layer,
// and repeated notes must not alternate to resonance without the sustain pedal.
#include "../src/modules/StagePianoEngine.h"
#include "../src/loaders/BankLoader.h"
#include <memory>
#include <vector>
#include <iostream>
#include <cmath>
using namespace SonusDSP;
int main(int argc,char**argv){
 auto bank=std::make_shared<PianoInstrument>();
 bank->isNpno=true; bank->formatType="Piano NPNO v5.30";
 for(int b=0;b<2;++b)for(int k=0;k<2;++k){
   StrokeSample s;s.index=int(bank->strokes.size());s.flags=uint8_t(b);s.zone=0;
   s.npnoLayer=uint8_t(k?15:0);s.velocityFrom=int(s.npnoLayer)*5;
   s.storedRoot=65;s.actualRootMidi=69;s.lokey=60;s.hikey=80;
   s.frames=8192;s.sampleScale=1.f/32768.f;s.pcmL.resize(s.frames);s.pcmR.resize(s.frames);
   for(size_t i=0;i<s.frames;++i){
      int16_t v=int16_t(8000*std::sin(i*0.045+b*0.2));s.pcmL[i]=v;s.pcmR[i]=v;
   }
   s.readyFrames->store(s.frames);
   bank->strokes.push_back(std::move(s));
 }
 StagePianoEngine host;
 host.setLayerInstrument(0,bank);
 std::vector<float> out(256*2);
 auto render=[&](){host.render(out.data(),256);};
 render();
 auto verify=[&](int vel,int expected){
    host.noteOn(69,vel);render();
    int idx=host.getLastStrokeIndex();
    if(idx!=expected){std::cerr<<"velocity="<<vel<<" expected stroke "<<expected<<" got "<<idx<<"\n";exit(2);}
    host.noteOff(69);render();
 };
 verify(127,0);verify(127,0); // no pedal: do not alternate to resonance
 verify(50,1); // softness floor((127-50)*31/127)=18 -> layer15
 host.setSustainPedal(true);render();
 verify(127,2);verify(50,3);
 host.setSustainPedal(false);render();
 verify(127,0);
 if(argc>1){
    PianoInstrument real;std::string msg;
    if(!BankLoader::loadNpnoNative(argv[1],real,msg)){std::cerr<<msg;return 3;}
    int attacks=0,resonance=0,release=0;
    for(auto& s:real.strokes){
      if(s.flags==0)++attacks;else if(s.flags==1)++resonance;else if(s.flags==2)++release;
    }
    if(attacks!=201||resonance!=201||release!=145||real.zoneCount!=32||!real.isNpno){
      std::cerr<<"Real NPno metadata mismatch\n";return 4;
    }
    if(!(real.strokes[0].levelGainLinear>1.f && real.strokes[0].npnoLayer==0 &&
         std::abs(real.npnoKeyTuneCents[21]+4.2f)<0.01f)){
       std::cerr<<"Real NPno gain/tuning mismatch\n";return 5;
    }
    std::cout<<"PASS real NPno metadata (+6 dB global, -4 dB trim; per-key tuning)\n";
 }
 std::cout<<"PASS NPno pedal attack/resonance and velocity layer selection\n";
}
