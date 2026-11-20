#include "../src/modules/StageOrganModule.h"
#include <vector>
#include <cmath>
#include <cassert>
#include <cstdio>
#include <algorithm>
using namespace SonusDSP;
static std::vector<float> sound(int model,float mix,bool fast,float sampleRate){
 StageOrganModule org;org.init(sampleRate);org.organEnabled=true;org.organModel=model;
 org.organRotaryFast=fast;org.rotaryMix=mix;org.organDrive=.05f;org.percussionEnabled=false;
 org.drawbars={1.0f,0.8f,0.9f,0.5f,0.3f,0.2f,0.12f,0.1f,0.04f};
 org.noteOn(60,60,100);
 std::vector<float> buffer(static_cast<size_t>(sampleRate*.40)*2,0.f);
 for(size_t n=0;n<buffer.size()/2;n+=128){const unsigned count=static_cast<unsigned>(std::min(size_t(128),buffer.size()/2-n));org.render(buffer.data()+n*2,count);}
 return buffer;
}
int main(){
 for(float sampleRate:{44100.f,48000.f,96000.f}){
  std::vector<std::vector<float>> models;
  for(int m=0;m<8;m++){
   auto s=sound(m,1.f,true,sampleRate);double rms=0,lr=0;
   for(size_t n=0;n<s.size()/2;n++){
    const float l=s[n*2],r=s[n*2+1];
    assert(std::isfinite(l)&&std::isfinite(r));
    assert(std::abs(l)<5.f&&std::abs(r)<5.f);
    rms+=double(l)*l;lr+=double(l-r)*(l-r);
   }
   rms=std::sqrt(rms/(s.size()/2));lr=std::sqrt(lr/(s.size()/2));
   assert(rms>0.01 && rms<1.4);assert(lr>0.001);
   models.push_back(std::move(s));
  }
  for(size_t m=1;m<models.size();m++){
   double meanDiff=0;
   for(size_t i=2000;i<models[m].size();i+=2)meanDiff+=std::abs(models[0][i]-models[m][i]);
   meanDiff/=(models[m].size()-2000)/2;
   assert(meanDiff>0.001);
  }
  auto dry=sound(0,0.f,false,sampleRate);
  for(size_t n=0;n<dry.size()/2;n++) assert(std::abs(dry[n*2]-dry[n*2+1])<1e-6);
  printf("PASS organ 8 timbres / Leslie stereo / dry bypass @ %.0f Hz\n",sampleRate);
 }
}
