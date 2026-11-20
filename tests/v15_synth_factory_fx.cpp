#include "../src/modules/StageSynthModule.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
using namespace SonusDSP;
static bool usable(const std::vector<float>& samples,double& energy){
  energy=0;for(float s:samples){if(!std::isfinite(s)||std::abs(s)>20.f)return false;energy+=double(s)*s;}return true;
}
int main(){
 for(int sr:{44100,48000,96000}){
  StageSynthModule module;module.init(float(sr));module.synthEnabled=true;
  if(module.getWavetableBank().size()!=24){std::cerr<<"bank size";return 1;}
  std::vector<float> out(256*2);
  int counts[5]{};int wavetableCount=0,seqCount=0,chorusCount=0,phaserCount=0,delayCount=0;
  for(int id=1;id<=256;id++){
   module.loadBuiltInSynthPreset(id);
   const int n=std::count_if(module.layers.begin(),module.layers.end(),[](auto& l){return l.cfg.enabled;});
   if(n<1||n>4)return 2;
   counts[n]++;
   for(const auto& l:module.layers)if(l.cfg.enabled){
    wavetableCount+=l.cfg.synthesisType==2;seqCount+=l.cfg.seqEnabled;
    chorusCount+=l.cfg.insertFx[0].enabled||l.cfg.insertFx[1].enabled;
    phaserCount+=l.cfg.insertFx[2].enabled||l.cfg.insertFx[3].enabled;
    delayCount+=l.cfg.insertFx[4].enabled;
   }
   std::fill(out.begin(),out.end(),0);
   module.render(out.data(),256);
   double energy=0;if(!usable(out,energy)||energy>1e-12){std::cerr<<"ghost preset "<<id<<" e="<<energy;return 3;}
   module.noteOn(60,60,100);
   energy=0;
   for(int block=0;block<15;block++){
     std::fill(out.begin(),out.end(),0);
     module.render(out.data(),256);
     double e=0;if(!usable(out,e)){std::cerr<<"invalid preset "<<id;return 4;}
     energy+=e;
   }
   if(energy<1e-8){std::cerr<<"silent preset "<<id<<" e="<<energy;return 5;}
  }
  if(counts[1]<32||counts[2]<32||counts[3]<32||counts[4]<32||wavetableCount<75||seqCount<30||chorusCount<50||phaserCount<20||delayCount<20){
    std::cerr<<"diversity failed "<<counts[1]<<","<<counts[2]<<","<<counts[3]<<","<<counts[4]<<" wt "<<wavetableCount<<" seq "<<seqCount<<" chorus "<<chorusCount<<" phaser "<<phaserCount<<" delay "<<delayCount;return 6;
  }
  std::cout<<"PASS "<<sr<<" Hz: 256 audible factory programs; layers "<<counts[1]<<"/"<<counts[2]<<"/"<<counts[3]<<"/"<<counts[4]<<"; WT "<<wavetableCount<<" sequenced "<<seqCount<<" chorus "<<chorusCount<<" phaser "<<phaserCount<<" delay "<<delayCount<<"\n";
 }
 // No patch mutations when silencing voices; independent per-layer insert stems.
 StageSynthModule module;module.init(48000);module.synthEnabled=true;module.loadBuiltInSynthPreset(16);
 auto before=module.layers[0].cfg;
 module.allNotesOff();
 if(module.layers[0].cfg.waveType!=before.waveType||module.layers[0].cfg.insertFx[0].enabled!=before.insertFx[0].enabled)return 7;
 module.loadBuiltInSynthPreset(4);
 for(auto& layer:module.layers)for(auto& fx:layer.cfg.insertFx)fx.enabled=false;
 std::vector<float> out(512*2,0.f);
 std::array<std::vector<float>,4> stems;std::array<float*,4> pointers{};
 for(int i=0;i<4;i++){stems[i].assign(out.size(),0.f);pointers[i]=stems[i].data();}
 module.noteOn(60,60,112);module.render(out.data(),512,&pointers);
 double total=0;for(int i=0;i<4;i++){double e=0;if(!usable(stems[i],e)||e<1e-8)return 8;total+=e;}
 module.allNotesOff();module.layers[2].cfg.insertFx[0].enabled=true;module.layers[2].cfg.insertFx[0].mix=.8f;
 for(auto& s:stems)std::fill(s.begin(),s.end(),0);
 module.noteOn(60,60,112);module.render(out.data(),512,&pointers);
 double fxEnergy=0;usable(stems[2],fxEnergy);
 if(std::fabs(fxEnergy-total/4.)<1e-15)return 9;
 // Regression: mono lead patches must actually use per-layer glide, not global poly mode.
 module.loadBuiltInSynthPreset(65);
 if(module.synthVoiceMaster || !module.layers[0].cfg.portamentoEnabled || module.layers[0].cfg.voiceMode!=1)return 12;
 module.noteOn(60,60,112);
 if(module.layers[0].voiceMode!=1 || !module.layers[0].glideEnabled)return 13;
 module.allNotesOff();
 // Bandlimited mipmaps and morph endpoints must produce observably different waves.
 const auto& bank=module.getWavetableBank();
 if(std::equal(bank[8].mip[0].begin(),bank[8].mip[0].end(),bank[9].mip[0].begin()))return 10;
 for(const auto& wt:bank)for(const auto& mip:wt.mip)for(float sample:mip)if(!std::isfinite(sample)||std::abs(sample)>2)return 11;
 std::cout<<"PASS independent stems, insert processing, allNotesOff and wavetable mips\n";
}
