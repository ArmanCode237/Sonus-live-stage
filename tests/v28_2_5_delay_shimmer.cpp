// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// v28.2.5 regression: independent TAP/BPM per Delay, NOTE even with SYNC off,
// and Shimmer v3 pitch layers / spectral-tone influence. No hardware required.
// g++ -std=c++17 -O2 -Isrc -Isrc/core -Isrc/dsp tests/v28_2_5_delay_shimmer.cpp -o /tmp/fxcheck && /tmp/fxcheck
#include "../src/dsp/StageSlotFxRack.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>
using namespace SonusDSP;
static int echoAt(int sr,StudioSlotConfig cfg,float globalBpm){
 StudioSlotDSP dsp;dsp.init(float(sr),5);
 const int len=sr*3;
 int first=-1;
 for(int i=0;i<len;i++){
   float l=0,r=0;
   dsp.process(i==0?.5f:0.f,0.f,cfg,l,r,globalBpm);
   if(!std::isfinite(l)||!std::isfinite(r))return -100;
   if(i>20 && std::abs(l)>0.05f){first=i;break;}
 }
 return first;
}
static double shimmerEnergy(int sr,StudioSlotConfig cfg){
 StudioSlotDSP dsp;dsp.init(float(sr),6);
 double energy=0;int count=0;
 for(int i=0;i<sr*2;i++){
  if((i&255)==0)dsp.syncParameters(cfg);
  float x=i<sr/2 ? (.12f*std::sin(float(2.*3.141592653589793*220.*i/sr)) + .07f*std::sin(float(2.*3.141592653589793*880.*i/sr)) + .05f*std::sin(float(2.*3.141592653589793*1760.*i/sr))) : 0.f;
  float l=0,r=0;dsp.process(x,x*.92f,cfg,l,r,120.f);
  assert(std::isfinite(l)&&std::isfinite(r));
  assert(std::abs(l)<12.f&&std::abs(r)<12.f);
  if(i>sr/2){energy+=l*l+r*r;count+=2;}
 }
 return std::sqrt(energy/std::max(1,count));
}
int main(){
 for(int sr:{44100,48000,96000}){
  StudioSlotConfig delay{};delay.type=5;delay.version=3;delay.enabled=true;
  delay.p.fill(.5f);delay.p[0]=.85f;delay.p[2]=0.f;delay.p[4]=0.f;
  delay.p[6]=0.f;delay.p[7]=(5.01f)/16.f; // quarter note
  delay.p[8]=(120.f-30.f)/270.f;
  const int localA=echoAt(sr,delay,90.f),localB=echoAt(sr,delay,180.f);
  assert(std::abs(localA-.5f*sr)<5);
  assert(localB==localA); // global BPM cannot affect unsynced slot
  delay.p[7]=(7.01f)/16.f; // eighth note
  const int eighth=echoAt(sr,delay,90.f);
  assert(std::abs(eighth-.25f*sr)<5);
  delay.p[6]=1.f; // sync = only BPM source changed
  const int synced=echoAt(sr,delay,150.f);
  assert(std::abs(synced-.2f*sr)<5);
  delay.p[6]=0.f;delay.p[8]=(75.f-30.f)/270.f;
  const int local75=echoAt(sr,delay,150.f);
  assert(std::abs(local75-.4f*sr)<5);
  StudioSlotConfig sh{};sh.type=6;sh.enabled=true;sh.version=3;
  sh.p.fill(.5f);sh.p[0]=.85f;sh.p[1]=.3f;sh.p[2]=.95f;
  sh.p[5]=.875f;sh.p[6]=.75f;sh.p[7]=.8f;sh.p[8]=.1f;sh.p[9]=.9f;
  sh.p[12]=.30f;sh.p[13]=.80f;sh.p[14]=.25f;sh.p[15]=.85f;
  sh.p[16]=(.64583f);sh.p[17]=.4f;sh.p[18]=.5f;sh.p[19]=.5f;sh.p[20]=.85f;
  const double base=shimmerEnergy(sr,sh);
  sh.p[19]=.0f;const double darker=shimmerEnergy(sr,sh);
  sh.p[19]=1.f;const double brighter=shimmerEnergy(sr,sh);
  sh.p[6]=.25f;const double pitchDown=shimmerEnergy(sr,sh);
  sh.p[6]=.75f;sh.p[12]=.51f;sh.p[16]=(.64583f);const double dualA=shimmerEnergy(sr,sh);
  sh.p[16]=(.25f);const double dualB=shimmerEnergy(sr,sh);
  sh.p[12]=.30f;sh.p[20]=0.f;const double layerAOff=shimmerEnergy(sr,sh);
  sh.p[20]=1.f;const double layerAOn=shimmerEnergy(sr,sh);
  assert(base>1.e-6&&darker>1.e-6&&brighter>1.e-6);
  assert(std::abs(brighter-darker)>1.e-5);
  assert(std::abs(dualA-dualB)>1.e-5);
  assert(std::abs(brighter-pitchDown)>1.e-5);
  assert(std::abs(layerAOn-layerAOff)>1.e-5);
  std::cout<<sr<<" Hz: delay note/sync/local PASS, shimmer rms base="<<base
           <<" dark="<<darker<<" bright="<<brighter<<" B(+7/-12)="<<dualA<<'/'<<dualB<<"\n";
 }
 std::cout<<"PASS v28.2.5 standalone Delay/Shimmer\n";
}
