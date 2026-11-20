// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// v28.2.9: Shimmer Atmosphere v4.2 regression (adds stereo coherence + early reflections vs Hall) (wet level, BLOOM, FREEZE, pitch return, stability, CPU).
// g++ -std=c++17 -O2 -Isrc -Isrc/core -Isrc/dsp tests/v28_2_9_shimmer_fix.cpp -o /tmp/shfix && /tmp/shfix
#include "../src/dsp/StageSlotFxRack.h"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace SonusDSP;
static StudioSlotConfig base(int model=3,int ver=4){
 StudioSlotConfig c;c.type=6;c.enabled=true;c.version=ver;
 c.p={{1.f,.55f,.72f,0.f,.80f,(model+.01f)/4.f,.75f,.84f,.25f,.48f,.30f,.15f,.30f,1.f,1.f,.8f,
       .6458333f,.35f,.50f,.50f,.70f,.50f,.65f,0.f}};return c;}
struct R{double rmsLate=0,rmsIn=0,peak=0;};
static R run(StudioSlotConfig c,int sr,double secs,double inSecs,double f0,int freezeAt=-1){
 StudioSlotDSP d;d.init(float(sr),6);R r;double a=0,b=0;long n=0,m=0;
 for(long i=0;i<long(secs*sr);++i){
  if(freezeAt>=0&&i==long(freezeAt)*sr)c.p[23]=1.f;
  if((i&63)==0)d.syncParameters(c);
  const double t=double(i)/sr;
  const float x=t<inSecs?float(.2*std::sin(6.283185307*f0*t)+.1*std::sin(6.283185307*f0*2.01*t)):0.f;
  float l=0,rr=0;d.process(x,x,c,l,rr,120.f);
  assert(std::isfinite(l)&&std::isfinite(rr));
  r.peak=std::max(r.peak,double(std::max(std::abs(l),std::abs(rr))));
  if(t<inSecs){a+=x*x;++n;}
  if(t>secs-1.0){b+=l*l;++m;}
 }
 r.rmsIn=std::sqrt(a/std::max(1L,n));r.rmsLate=std::sqrt(b/std::max(1L,m));return r;}
int main(){
 for(int sr:{44100,48000,96000}){
  // 1) wet level: must be of the same order as Hall (was ~18 dB lower)
  auto sh=run(base(3,4),sr,3,3,261.6),hall=run(base(2,3),sr,3,3,261.6);
  const double dB=20*std::log10(sh.rmsLate/hall.rmsLate);
  std::cout<<sr<<" Hz: shimmer v4 vs hall steady wet = "<<dB<<" dB\n";
  assert(dB>-9.0 && dB<3.0);
  // 2) FREEZE keeps sustaining 20 s after the input stopped, release decays
  auto c=base();c.p[7]=.5f;
  auto fz=run(c,sr,20,1,330,2);
  auto nofz=run(c,sr,20,1,330,-1);
  std::cout<<"   FREEZE rms@19-20s="<<fz.rmsLate<<" no-freeze="<<nofz.rmsLate<<"\n";
  assert(fz.rmsLate>1e-3 && fz.rmsLate>nofz.rmsLate*20);
  // 3) PITCH RETURN audible: wet energy rises with it
  auto p0=base();p0.p[2]=0.f;auto p1=base();p1.p[2]=1.f;
  assert(run(p1,sr,3,3,440).rmsLate>run(p0,sr,3,3,440).rmsLate*1.05);
  // 4) BLOOM is not a plain volume knob: level changes <6 dB between extremes
  auto b0=base();b0.p[22]=0;auto b1=base();b1.p[22]=1;
  const double bd=std::abs(20*std::log10(run(b1,sr,3,3,261.6).rmsLate/run(b0,sr,3,3,261.6).rmsLate));
  assert(bd<6.0); // BLOOM shapes the build-up; it is not a volume knob
  // 5) worst-case stability: bounded for 40 s
  auto w=base();for(int k:{7,2,20,17,22,4,9})w.p[k]=1.f;w.p[1]=0;w.p[12]=.5f;
  assert(run(w,sr,40,10,220).peak<1.3);
 }
 // 5b) stereo image like Hall: positive L/R correlation, early reflections within 15 ms
 {
  StudioSlotDSP d;d.init(48000.f,6);auto c=base();std::vector<float> L,R;unsigned s=12345;
  for(long i=0;i<48000L*4;++i){
   if((i&63)==0)d.syncParameters(c);
   s=s*1664525u+1013904223u;const float x=i<48000*2?(float(s>>8)/float(1<<24)-.5f)*.2f:0.f;
   float l=0,r=0;d.process(x,x,c,l,r,120.f);if(i>14400&&i<48000*2){L.push_back(l);R.push_back(r);}
  }
  double a=0,b=0,cr=0;for(size_t i=0;i<L.size();++i){a+=L[i]*L[i];b+=R[i]*R[i];cr+=L[i]*R[i];}
  const double corr=cr/std::sqrt(a*b+1e-30);std::cout<<"L/R correlation="<<corr<<"\n";
  assert(corr>.2 && corr<.8);
  StudioSlotDSP e;e.init(48000.f,6);auto c2=base();c2.p[22]=0.f;int onset=-1;float pk=0;std::vector<float> w;
  for(int i=0;i<4800;++i){if((i&63)==0)e.syncParameters(c2);float l=0,r=0;e.process(i<3?.5f:0.f,i<3?.5f:0.f,c2,l,r,120.f);w.push_back(l);pk=std::max(pk,std::abs(l));}
  for(int i=0;i<4800;++i)if(std::abs(w[i])>pk*.3f){onset=i;break;}
  std::cout<<"first early reflection at "<<onset/48.0<<" ms\n";assert(onset>=96 && onset<720);
 }
 // 6) CPU in long silence (denormals): far below real time
 auto c=base();StudioSlotDSP d;d.init(48000.f,6);d.syncParameters(c);
 auto t0=std::chrono::steady_clock::now();
 for(long i=0;i<48000L*20;++i){float l=0,r=0;d.process(i<100?.4f:0.f,i<100?.4f:0.f,c,l,r,120.f);}
 const double s=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
 std::cout<<"20 s of audio in "<<s<<" s (silence tail)\n";assert(s<20*.2);
 std::cout<<"PASS v28.2.9 shimmer v4.1\n";}
