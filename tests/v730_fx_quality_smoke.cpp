// g++ -std=c++17 -O2 -Isrc -Isrc/core -Isrc/modules tests/v730_fx_quality_smoke.cpp -o /tmp/v730_fx && /tmp/v730_fx
#include "../src/dsp/StageSlotFxRack.h"
#include <iostream>
#include <cmath>
#include <algorithm>
using namespace SonusDSP;
int main(){
 for(int rate: {44100,48000,96000}) {
   for(int type=1;type<=11;++type){
     if(rate!=48000 && type!=6)continue;
     StudioSlotDSP dsp; dsp.init(float(rate),type);
     StudioSlotConfig cfg; cfg.type=type;cfg.enabled=true;
     cfg.p[0]=.7f;cfg.p[1]=.8f;cfg.p[2]=.6f;cfg.p[3]=.8f;cfg.p[4]=.8f;cfg.p[5]=.9f;
     if(type==6){cfg.p[3]=.0f;cfg.p[5]=.76f;cfg.p[6]=1;cfg.p[7]=.35f;cfg.p[8]=.2f;cfg.p[9]=.45f;cfg.p[10]=.12f;cfg.p[11]=.08f;cfg.p[12]=.5f;cfg.p[13]=1;cfg.p[14]=1;cfg.p[15]=.75f;}
     double energy=0;float peak=0;
     int total=rate*(type==6||type==5?2:1);
     for(int n=0;n<total;++n){
       float left=(n<rate/3 ? .25f*std::sin(2*3.14159265358979f*220.f*n/rate) :0.f);
       float right=(n<rate/3 ? .21f*std::sin(2*3.14159265358979f*311.f*n/rate) :0.f);
       float wetL=0,wetR=0;
       dsp.process(left,right,cfg,wetL,wetR);
       if(!std::isfinite(wetL)||!std::isfinite(wetR)){std::cerr<<"nan type "<<type<<" rate "<<rate<<'\n';return 1;}
       peak=std::max({peak,std::abs(wetL),std::abs(wetR)});
       energy+=double(wetL)*wetL+double(wetR)*wetR;
     }
     if(energy<1.e-11) {std::cerr<<"SILENT EFFECT "<<type<<" @ "<<rate<<"\n";return 2;}
     if(peak>3.0001f){std::cerr<<"unsafe peak "<<peak<<" type "<<type<<"\n";return 3;}
     std::cout<<"PASS fx "<<type<<" @ "<<rate<<" energy "<<energy<<" peak "<<peak<<"\n";
   }
 }
}
