#include "Sf2Loader.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <map>
#include <vector>

namespace SonusDSP {
namespace {
using Bytes=std::vector<uint8_t>;
uint16_t u16(const uint8_t* p){return uint16_t(p[0]|(uint16_t(p[1])<<8));}
uint32_t u32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
int16_t i16(const uint8_t* p){return int16_t(u16(p));}
std::string name(const uint8_t* p,size_t n){size_t i=0;while(i<n&&p[i])++i;return std::string(reinterpret_cast<const char*>(p),i);}
bool tag(const uint8_t* p,const char* t){return std::memcmp(p,t,4)==0;}
struct Chunk{size_t offset=0,length=0;};
struct Data {
 Bytes bytes;std::map<std::string,Chunk> chunks;
 bool read(const std::string& path,std::string& error){
  std::ifstream in(path,std::ios::binary|std::ios::ate);
  if(!in){error="No se pudo abrir SF2";return false;}
  auto len=in.tellg();if(len<12||len>std::streamoff(768)*1024*1024){error="SF2 inválido o demasiado grande (>768 MiB)";return false;}
  bytes.resize(size_t(len));in.seekg(0);if(!in.read(reinterpret_cast<char*>(bytes.data()),len)){error="Lectura SF2 incompleta";return false;}
  if(!tag(bytes.data(),"RIFF")||!tag(bytes.data()+8,"sfbk")){error="SF2 requiere RIFF/sfbk";return false;}
  const size_t limit=std::min(bytes.size(),size_t(u32(bytes.data()+4))+8);
  for(size_t pos=12;pos+8<=limit;){
   const size_t n=u32(bytes.data()+pos+4);if(n>limit-pos-8)break;
   const uint8_t* p=bytes.data()+pos;
   if(tag(p,"LIST")&&n>=4){
    const std::string group(reinterpret_cast<const char*>(p+8),4);
    if(group=="pdta"||group=="sdta"){
     const size_t end=pos+8+n;
     for(size_t q=pos+12;q+8<=end;){
      const size_t sub=u32(bytes.data()+q+4);if(sub>end-q-8)break;
      chunks[std::string(reinterpret_cast<const char*>(bytes.data()+q),4)]={q+8,sub};
      q+=8+sub+(sub&1);
     }
    }
   }
   pos+=8+n+(n&1);
  }
  for(const auto& k:{"phdr","pbag","pgen","inst","ibag","igen","shdr","smpl"}){
   if(!chunks.count(k)){error=std::string("SF2 sin sección ")+k;return false;}
  }
  return true;
 }
 bool valid(const char* k,size_t stride,size_t minEntries=1)const{
  const auto& c=chunks.at(k);return c.length%stride==0&&c.length/stride>=minEntries;
 }
 const uint8_t* ptr(const char* k,size_t stride,size_t idx)const{
  const auto& c=chunks.at(k);return c.offset+idx*stride+stride<=c.offset+c.length?bytes.data()+c.offset+idx*stride:nullptr;
 }
 size_t count(const char* k,size_t stride)const{return chunks.at(k).length/stride;}
};
// Reuse the parsed RIFF and PCM data while browsing patches from one SF2.
// The cache is bounded to a single bank and invalidated on path/size/mtime
// changes; big banks are released immediately after their callers finish.
std::shared_ptr<const Data> sharedSoundFontData(const std::string& file,std::string& error){
 namespace fs=std::filesystem;
 static std::mutex cacheMutex;
 static std::string cachedPath;
 static std::uintmax_t cachedSize=0;
 static fs::file_time_type cachedTime{};
 static std::shared_ptr<const Data> cached;
 std::error_code ec;
 const auto size=fs::file_size(file,ec);
 if(ec){error="SF2 no accesible";return {};}
 const auto modified=fs::last_write_time(file,ec);
 if(ec){error="No se pudo verificar SF2";return {};}
 {
  std::lock_guard<std::mutex> guard(cacheMutex);
  if(cached&&cachedPath==file&&cachedSize==size&&cachedTime==modified)return cached;
  cached.reset();cachedPath.clear();cachedSize=0;
 }
 auto fresh=std::make_shared<Data>();
 if(!fresh->read(file,error))return {};
 if(size<=192ULL*1024*1024){
  std::lock_guard<std::mutex> guard(cacheMutex);
  cachedPath=file;cachedSize=size;cachedTime=modified;cached=fresh;
 }
 return fresh;
}
struct Zone {
 std::map<int,int> generators;
 int loKey=0,hiKey=127,loVel=0,hiVel=127;
 void apply(int op,uint16_t amount){
  if(op==43||op==44){int a=amount&255,b=amount>>8;
   if(op==43){loKey=std::max(loKey,a);hiKey=std::min(hiKey,b);}
   else{loVel=std::max(loVel,a);hiVel=std::min(hiVel,b);}return;}
  generators[op]=(op==41||op==53||op==54||op==58)?int(amount):int(int16_t(amount));
 }
 int get(int op,int fallback=0)const{auto it=generators.find(op);return it==generators.end()?fallback:it->second;}
};
Zone combine(Zone a,const Zone& b){
 a.loKey=std::max(a.loKey,b.loKey);a.hiKey=std::min(a.hiKey,b.hiKey);
 a.loVel=std::max(a.loVel,b.loVel);a.hiVel=std::min(a.hiVel,b.hiVel);
 // additive generators can inherit from preset and instrument; use overrides for non-additive.
 for(const auto& kv:b.generators){
  if(kv.first==41||kv.first==53||kv.first==54||kv.first==58||kv.first==47) a.generators[kv.first]=kv.second;
  else a.generators[kv.first]+=kv.second;
 }
 return a;
}
std::vector<Zone> zones(const Data& d,const char* bag,const char* gen,size_t begin,size_t end){
 std::vector<Zone> out;const size_t n=d.count(bag,4);end=std::min(end,n);
 for(size_t i=begin;i<end;i++){
  const auto* a=d.ptr(bag,4,i);if(!a)break;
  const auto* b=d.ptr(bag,4,i+1);if(!b)break;
  Zone z;const size_t g0=u16(a),g1=std::min<size_t>(u16(b),d.count(gen,4));
  for(size_t j=g0;j<g1;j++){const auto* g=d.ptr(gen,4,j);if(g)z.apply(u16(g),u16(g+2));}
  out.push_back(std::move(z));
 }
 return out;
}
struct SampleHeader {std::string label;uint32_t start,end,startLoop,endLoop,sr;int pitch,correction,link,type;};
SampleHeader sampleHeader(const uint8_t* p){return {name(p,20),u32(p+20),u32(p+24),u32(p+28),u32(p+32),u32(p+36),p[40],int(int8_t(p[41])),int(u16(p+42)),int(u16(p+44))};}
}
std::vector<std::string> Sf2Loader::listPresets(const std::string& file){
 std::string error;auto shared=sharedSoundFontData(file,error);if(!shared)return {};
 const Data& d=*shared;if(!d.valid("phdr",38)||!d.valid("inst",22,2))return {};
 std::vector<std::string> result;
 if(d.count("phdr",38)==1){ // Instrument-only RIFF banks with only EOP sentinel.
  for(size_t i=0;i+1<d.count("inst",22);++i){
   const auto* inst=d.ptr("inst",22,i);
   result.push_back(std::to_string(i)+" · "+name(inst,20)+" (instrument-only SF2)");
  }
  return result;
 }
 for(size_t i=0;i+1<d.count("phdr",38);++i){const auto*p=d.ptr("phdr",38,i);
  result.push_back(std::to_string(i)+" · "+name(p,20)+" (B"+std::to_string(u16(p+22))+" P"+std::to_string(u16(p+20))+")");}
 return result;
}
bool Sf2Loader::load(const std::string& file,PianoInstrument& out,std::string& error,int presetIndex){
 auto shared=sharedSoundFontData(file,error);if(!shared)return false;
 const Data& d=*shared;
 if(!d.valid("phdr",38)||!d.valid("pbag",4)||!d.valid("pgen",4)||
    !d.valid("inst",22,2)||!d.valid("ibag",4,2)||!d.valid("igen",4)||!d.valid("shdr",46,2)||!d.valid("smpl",2,8)){
  error="Tablas de SoundFont incompletas o corruptas";return false;}
 const bool instrumentOnly=d.count("phdr",38)==1;
 const size_t presetCount=instrumentOnly?d.count("inst",22)-1:d.count("phdr",38)-1;
 if(presetIndex<0){presetIndex=0;
  if(!instrumentOnly){ // Prefer a melodic bank, not percussion.
   for(size_t i=0;i<presetCount;i++){auto*p=d.ptr("phdr",38,i);if(u16(p+22)!=128){presetIndex=int(i);break;}}
  }
 }
 if(presetIndex>=int(presetCount)){error="Índice de programa/instrumento SF2 fuera de rango";return false;}
 const auto* ph=instrumentOnly?nullptr:d.ptr("phdr",38,size_t(presetIndex));
 std::vector<Zone> pzones;
 if(instrumentOnly){ // Synthetic preset region referencing the actual inst table.
  Zone direct;direct.generators[41]=presetIndex;pzones.push_back(direct);
 }else{
  const auto* pn=d.ptr("phdr",38,size_t(presetIndex+1));
  pzones=zones(d,"pbag","pgen",u16(ph+24),u16(pn+24));
 }
 Zone presetGlobal;bool globalFound=false;
 const size_t pcmFrames=d.chunks.at("smpl").length/2;
 const uint8_t* pcm=d.bytes.data()+d.chunks.at("smpl").offset;
 std::vector<StrokeSample> strokes;size_t usedBytes=0;constexpr size_t MAX_PCM=512ULL*1024*1024;
 for(const auto& local:pzones){
  if(!local.generators.count(41)){if(!globalFound){presetGlobal=local;globalFound=true;}continue;}
  Zone preset=combine(presetGlobal,local);
  const int instrument=local.get(41,-1);
  if(instrument<0||size_t(instrument)+1>=d.count("inst",22))continue;
  const auto* ih=d.ptr("inst",22,instrument);const auto* in=d.ptr("inst",22,instrument+1);
  Zone instGlobal;bool instGlobalFound=false;
  for(const auto& ilocal:zones(d,"ibag","igen",u16(ih+20),u16(in+20))){
   if(!ilocal.generators.count(53)){if(!instGlobalFound){instGlobal=ilocal;instGlobalFound=true;}continue;}
   const Zone z=combine(combine(preset,instGlobal),ilocal);
   if(z.loKey>z.hiKey||z.loVel>z.hiVel)continue;
   const int sid=ilocal.get(53,-1);
   if(sid<0||size_t(sid)+1>=d.count("shdr",46))continue;
   const auto* sh=d.ptr("shdr",46,sid);SampleHeader h=sampleHeader(sh);
   if(h.sr<4000||h.sr>192000||h.end<=h.start||h.end>pcmFrames)continue;
   // Generators start/end offsets: coarse offsets are in units of 32768 frames.
   const auto offset=[&](int base,int coarse){return int64_t(z.get(base))+int64_t(z.get(coarse))*32768;};
   const int64_t start=std::clamp<int64_t>(int64_t(h.start)+offset(0,4),0,int64_t(pcmFrames));
   const int64_t end=std::clamp<int64_t>(int64_t(h.end)+offset(1,12),0,int64_t(pcmFrames));
   if(end-start<4)continue;
   const size_t length=size_t(end-start);
   if(length>MAX_PCM/8||usedBytes>MAX_PCM-length*4){error="SF2 supera límite de 512 MiB decodificados";return false;}
   StrokeSample s;
   s.index=int(strokes.size());s.lokey=z.loKey;s.hikey=z.hiKey;
   s.lovel=std::max(1,z.loVel);s.hivel=std::max(s.lovel,z.hiVel);
   s.sampleRate=h.sr;s.frames=uint32_t(length);
   s.actualRootMidi=std::clamp(z.get(58,h.pitch),0,127);
   s.fineTuneCents=float(h.correction+z.get(52)+100*z.get(51));
   const float atten=float(std::clamp(z.get(48),0,1440));
   s.levelGainLinear=std::pow(10.f,-atten/200.f); // SF2 centibels
   s.pcmL.resize(length);s.pcmR.resize(length);s.sampleScale=1.f/32768.f;
   // A stereo link can point at the opposite channel's sample header.
   SampleHeader other{};bool stereo=false;
   if((h.type&0x7)==2||(h.type&0x7)==4){
    if(h.link>=0&&size_t(h.link)+1<d.count("shdr",46)){
     other=sampleHeader(d.ptr("shdr",46,h.link));
     stereo=other.end>other.start&&other.end<=pcmFrames&&other.end-other.start>=length &&
         ((other.type&7)==((h.type&7)==2?4:2));
    }
   }
   if(stereo && (h.type&7)==2)continue; // The matching LEFT zone will create one stereo voice.
   for(size_t f=0;f<length;f++){
    const int16_t x=i16(pcm+(start+int64_t(f))*2);
    const int16_t y=stereo?i16(pcm+(int64_t(other.start)+int64_t(f))*2):x;
    s.pcmL[f]=(h.type&7)==2?y:x;
    s.pcmR[f]=(h.type&7)==4?y:x;
   }
   const int mode=z.get(54,0)&3;
   s.sf2LoopMode=static_cast<uint8_t>(mode==1||mode==3?mode:0);
   if(mode==1||mode==3){
    const int64_t ls=int64_t(h.startLoop)+offset(2,45)-start;
    const int64_t le=int64_t(h.endLoop)+offset(3,50)-start;
    if(ls>=0&&le>ls+16&&le<=int64_t(length)){
     s.loop.hasLoop=true;s.loop.start=uint32_t(ls);s.loop.end=uint32_t(le);s.loop.length=uint32_t(le-ls);
    }
   }
   // Each SF2 zone is a distinct key/velocity region, not a velocity layer
   // of a single hardware zone. Do not let selectStrokeFor() collapse them.
   s.zone=s.index; s.layerIndex=0;
   s.loopDecayDbPerS=0.f;s.decayCoef=1.f;
   s.readyFrames->store(s.frames,std::memory_order_release);
   usedBytes+=length*4;strokes.push_back(std::move(s));
   if(strokes.size()>=4096)break;
  }
  if(strokes.size()>=4096)break;
 }
 if(strokes.empty()){error="SF2 válido, pero el preset no contiene regiones PCM16 reproducibles";return false;}
 out.strokes=std::move(strokes);out.name=instrumentOnly?name(d.ptr("inst",22,size_t(presetIndex)),20):name(ph,20);out.sourcePath=file;out.sf2PresetIndex=presetIndex;out.formatType="SF2 (PCM16)";
 out.zoneCount=int(out.strokes.size());out.memoryMB=usedBytes/(1024.0*1024.0);
 // A fallback global ID is fine, but ensure it differs from concurrently loaded banks.
 static std::atomic<uint64_t> nextId{1000000};out.id=nextId.fetch_add(1);
 error="SoundFont cargado: "+out.name+" ("+std::to_string(out.zoneCount)+" regiones)";
 return true;
}
} // namespace SonusDSP
