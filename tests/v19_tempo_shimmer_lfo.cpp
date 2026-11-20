#include "../src/dsp/StageTempo.h"
#include "../src/dsp/StageSlotFxRack.h"
#include "../src/modules/StageSynthModule.h"
#include <cmath>
#include <iostream>
#include <vector>
#include <algorithm>
using namespace SonusDSP;
static bool finite(float x){return std::isfinite(x)&&std::fabs(x)<20.f;}
static double slotEnergy(int model, float bpm, bool sync, int note){
 StudioSlotDSP fx;fx.init(48000,model);
 StudioSlotConfig cfg;cfg.type=model;cfg.enabled=true;cfg.p.fill(0.5f);
 if(model==5){cfg.p[0]=.8;cfg.p[2]=.2;cfg.p[3]=.18;cfg.p[4]=1.f;cfg.p[5]=.5;cfg.p[6]=sync?1.f:0.f;cfg.p[7]=(note+.01f)/16.f;}
 else {cfg.p[0]=.9f;cfg.p[1]=.3f;cfg.p[2]=.8f;cfg.p[3]=0.f;cfg.p[4]=.6f;cfg.p[5]=.99f;cfg.p[6]=1.f;cfg.p[7]=.5f;cfg.p[8]=.2f;cfg.p[9]=.78f;cfg.p[10]=.4f;cfg.p[11]=.18f;cfg.p[12]=.45f;cfg.p[13]=.85f;cfg.p[14]=.25f;cfg.p[15]=.85f;}
 double energy=0;int count=0;
 for(int i=0;i<48000*2;i++){
   const float s=i<6000 ? .21f*std::sin(float(i)*.045f) : 0.f;
   float l=0,r=0;fx.process(s,s,cfg,l,r,bpm);
   if(!finite(l)||!finite(r)){std::cerr<<"Invalid audio sample ";return -1;}
   if(i>18000){energy+=double(l)*l+double(r)*r;if(std::fabs(l)+std::fabs(r)>1e-6)count++;}
 }
 if(count<10)return -1;
 return energy;
}
static std::vector<float> synthStem(float lfoDepth, bool layer1Affected){
 StageSynthModule m;m.init(48000);m.synthEnabled=true;m.synthMasterVol=1;
 for(int i=0;i<4;i++){
  auto& c=m.layers[i].cfg;c.enabled=(i==0||i==3);c.synthesisType=0;c.waveType=3;c.volume=.65f;c.attackSec=.001f;c.decaySec=.4f;c.sustainLevel=.83f;c.releaseSec=.4f;c.pan=0;
  for(auto& fx:c.insertFx)fx.enabled=false;
  auto& l=m.lfos[i];l.enabled=(i==3)||(layer1Affected&&i==0);l.rateHz=9.f;l.depth=lfoDepth;l.destPitch=55;l.destCutoff=2000;l.destVolume=.6f;l.destPan=.4f;
 }
 m.noteOn(60,60,100);
 std::array<std::vector<float>,4> b;std::array<float*,4> ptr{};for(int i=0;i<4;i++){b[i].assign(4096*2,0.f);ptr[i]=b[i].data();}
 std::vector<float> out(4096*2);m.render(out.data(),4096,&ptr);
 // Interleave layer A and D to detect independence
 std::vector<float> both;b[0].insert(b[0].end(),b[3].begin(),b[3].end());return b[0];
}
int main(){
 if(std::fabs(tempoMilliseconds(120,5)-500.f)>1e-3)return 1;
 if(std::fabs(tempoMilliseconds(120,6)-750.f)>1e-3)return 2;
 if(std::fabs(tempoMilliseconds(120,8)-375.f)>1e-3)return 3;
 if(std::fabs(tempoMilliseconds(90,13)-444.444f)>1)return 4;
 for(float bpm:{80.f,120.f,172.f})for(int div=0;div<16;div++)if(!(tempoMilliseconds(bpm,div)>0))return 5;
 auto delayA=slotEnergy(5,120.f,true,7);auto delayB=slotEnergy(5,60.f,true,7);
 if(delayA<0||delayB<0){std::cerr<<"No delay tail\n";return 6;}
 auto shimmer=slotEnergy(6,120.f,false,5);
 if(shimmer<0){std::cerr<<"Shimmer did not generate wet tail\n";return 7;}
 const auto a=synthStem(0,false),b=synthStem(1.f,false),c=synthStem(1.f,true);
 double diffA=0,diffD=0,diffLayerA=0;
 for(size_t n=0;n<a.size()/2;n++)diffA+=std::fabs(double(a[n]-b[n]));
 for(size_t n=a.size()/2;n<a.size();n++)diffD+=std::fabs(double(a[n]-b[n]));
 for(size_t n=0;n<a.size()/2;n++)diffLayerA+=std::fabs(double(a[n]-c[n]));
 if(diffA>1e-4||diffD<1e-3||diffLayerA<1e-3){std::cerr<<"LFO isolation: "<<diffA<<' '<<diffD<<' '<<diffLayerA<<'\n';return 8;}
 std::cout<<"PASS BPM divisions/delay tails, shimmer wet tail energy="<<shimmer<<" and independent layer LFO\n";
}
