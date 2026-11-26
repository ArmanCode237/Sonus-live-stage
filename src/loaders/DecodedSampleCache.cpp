#include "DecodedSampleCache.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <set>
#include <chrono>
#include <mutex>
#include <sstream>
#include <thread>
#include <type_traits>
#ifdef _WIN32
#include <windows.h>
#endif
namespace fs=std::filesystem;
namespace SonusDSP {
namespace {
std::mutex directoryMutex;
fs::path chosenDirectory=".cache";
constexpr uint32_t cacheVersion=1;
constexpr uint32_t maxRecords=100000;
constexpr uint32_t maxString=16384;
constexpr uint32_t maxFramesPerStroke=100000000;
constexpr uint64_t maxPcmBytes=uint64_t(24)*1024*1024*1024;
constexpr char magic[8]={'N','V','S','P','C','M','0','1'};
uint64_t mix(uint64_t h,const void* b,size_t n){
    const auto* s=static_cast<const unsigned char*>(b);
    for(size_t i=0;i<n;++i){h^=s[i];h*=1099511628211ULL;}
    return h;
}
uint64_t stringHash(uint64_t h,const std::string& s){return mix(h,s.data(),s.size());}
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string canonical(const std::string& s){
    std::error_code ec;const auto p=fs::weakly_canonical(s,ec);
    return (ec?fs::path(s).lexically_normal():p).generic_string();
}
// Capture external WAV dependencies conservatively: changing, removing or adding a
// WAV below an SFZ folder invalidates its cache, including shared default_path files.
// This is deliberately conservative rather than interpreting SFZ syntax differently
// from the existing BankLoader parser.
bool fingerprint(const std::string& source,uint64_t& result){
    std::error_code ec;
    fs::path p=canonical(source);
    if(!fs::is_regular_file(p,ec) || ec)return false;
    uint64_t h=14695981039346656037ULL;
    auto stamp=[&](const fs::path& f)->bool{
        std::error_code e;const uint64_t size=fs::file_size(f,e);if(e)return false;
        const auto time=fs::last_write_time(f,e);if(e)return false;
        const int64_t t=static_cast<int64_t>(time.time_since_epoch().count());
        h=stringHash(h,f.lexically_normal().generic_string());
        h=mix(h,&size,sizeof(size));h=mix(h,&t,sizeof(t));return true;
    };
    if(!stamp(p))return false;
    if(lower(p.extension().string())==".sfz"){
        // A hash over a sorted list is stable across filesystem enumeration order.
        std::vector<std::pair<std::string,std::pair<uint64_t,int64_t>>> deps;
        std::set<std::string> locations;
        locations.insert(p.parent_path().lexically_normal().generic_string());
        // SFZ default_path may reference WAV outside the SFZ folder.
        // Track those directories as well rather than allowing stale audio.
        std::ifstream sfz(p,std::ios::binary|std::ios::ate);
        if(!sfz || sfz.tellg()>std::streampos(16*1024*1024))return false;
        sfz.seekg(0);const std::string contents((std::istreambuf_iterator<char>(sfz)),{});
        const std::regex prefixes(R"re(default_path\s*=\s*(?:"([^"]+)"|([^\s<>]+)))re",std::regex_constants::icase);
        for(auto i=std::sregex_iterator(contents.begin(),contents.end(),prefixes);i!=std::sregex_iterator();++i){
            std::string name=(*i)[1].matched?(*i)[1].str():(*i)[2].str();
            for(auto& c:name)if(c=='\\')c='/';
            auto parent=(p.parent_path()/name).lexically_normal();
            if(fs::is_directory(parent,ec)&&!ec)locations.insert(parent.generic_string());
            ec.clear();
        }
        for(const auto& location:locations){
            fs::recursive_directory_iterator it(location,fs::directory_options::skip_permission_denied,ec),end;
            if(ec)return false;
            for(;it!=end;it.increment(ec)){
                if(ec)return false;
                if(deps.size()>100000)return false;
                if(!it->is_regular_file(ec)){if(ec)return false;continue;}
                if(lower(it->path().extension().string())!=".wav")continue;
                const auto size=it->file_size(ec);if(ec)return false;
                const auto time=it->last_write_time(ec);if(ec)return false;
                deps.push_back({it->path().lexically_normal().generic_string(),
                    {size,static_cast<int64_t>(time.time_since_epoch().count())}});
            }
        }
        std::sort(deps.begin(),deps.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        deps.erase(std::unique(deps.begin(),deps.end(),[](const auto& a,const auto& b){return a.first==b.first;}),deps.end());
        const auto n=static_cast<uint64_t>(deps.size());h=mix(h,&n,sizeof(n));
        for(const auto& d:deps){h=stringHash(h,d.first);h=mix(h,&d.second.first,sizeof(d.second.first));h=mix(h,&d.second.second,sizeof(d.second.second));}
    }
    result=h;return true;
}
template<class T>bool read(std::istream& s,T& v){static_assert(std::is_trivially_copyable<T>::value,"POD only");return bool(s.read(reinterpret_cast<char*>(&v),sizeof(v)));}
template<class T>void write(std::ostream& s,const T& v){static_assert(std::is_trivially_copyable<T>::value,"POD only");s.write(reinterpret_cast<const char*>(&v),sizeof(v));}
bool readString(std::istream& s,std::string& v){uint32_t n=0;if(!read(s,n)||n>maxString)return false;v.resize(n);return n==0||bool(s.read(&v[0],n));}
void writeString(std::ostream& s,const std::string& v){const uint32_t n=static_cast<uint32_t>(v.size());write(s,n);s.write(v.data(),n);}
struct Header { char sign[8]; uint32_t version; uint32_t endian; uint64_t fingerprint; int32_t preset; uint32_t strokes; uint64_t pcmBytes; };
static_assert(std::is_trivially_copyable<Header>::value,"cache header POD");
Header makeHeader(uint64_t stamp,int preset,const PianoInstrument& bank){
    Header hdr{};std::memcpy(hdr.sign,magic,8);hdr.version=cacheVersion;hdr.endian=0x01020304;
    hdr.fingerprint=stamp;hdr.preset=preset;hdr.strokes=uint32_t(bank.strokes.size());
    for(const auto& s:bank.strokes)hdr.pcmBytes+=uint64_t(s.frames)*sizeof(int16_t)*2;
    return hdr;
}
bool headerValid(const Header& h,uint64_t stamp,int preset){
    return std::memcmp(h.sign,magic,8)==0 && h.version==cacheVersion && h.endian==0x01020304 &&
        h.fingerprint==stamp && h.preset==preset && h.strokes>0 && h.strokes<=maxRecords &&
        h.pcmBytes>0 && h.pcmBytes<=maxPcmBytes;
}
void writeStroke(std::ostream& f,const StrokeSample& s){
#define W(x) write(f,s.x)
    W(index);W(zone);W(layerIndex);W(totalLayersInZone);W(velocityFrom);W(switchPoint);
    W(flags);W(isReleaseStroke);W(alternateStartFrame);W(explicitOneShot);
    W(storedRoot);W(actualRootMidi);W(lokey);W(hikey);W(lovel);W(hivel);
    W(frames);W(startFrameOffset);W(sampleRate);
    W(loop.hasLoop);W(loop.isAutoLoop);W(loop.start);W(loop.length);W(loop.end);
    W(sf2LoopMode);W(fineTuneCents);W(stretchCents);W(baseLevelWord);
    W(levelGainLinear);W(loopDecayDbPerS);W(decayCoef);W(damperIndex);W(softDamperIndex);
    W(damperT60Seconds);W(softDamperT60Seconds);W(sampleScale);
#undef W
    const uint64_t published=s.publishedAutoLoop?s.publishedAutoLoop->load(std::memory_order_acquire):0;
    write(f,published);
    const uint64_t checksum=mix(mix(14695981039346656037ULL,s.pcmL.data(),s.pcmL.size()*2),s.pcmR.data(),s.pcmR.size()*2);
    write(f,checksum);
    f.write(reinterpret_cast<const char*>(s.pcmL.data()),s.pcmL.size()*2);
    f.write(reinterpret_cast<const char*>(s.pcmR.data()),s.pcmR.size()*2);
}
bool readStroke(std::istream& f,StrokeSample& s,uint64_t& accumulated){
#define R(x) if(!read(f,s.x))return false
    R(index);R(zone);R(layerIndex);R(totalLayersInZone);R(velocityFrom);R(switchPoint);
    R(flags);R(isReleaseStroke);R(alternateStartFrame);R(explicitOneShot);
    R(storedRoot);R(actualRootMidi);R(lokey);R(hikey);R(lovel);R(hivel);
    R(frames);R(startFrameOffset);R(sampleRate);
    R(loop.hasLoop);R(loop.isAutoLoop);R(loop.start);R(loop.length);R(loop.end);
    R(sf2LoopMode);R(fineTuneCents);R(stretchCents);R(baseLevelWord);
    R(levelGainLinear);R(loopDecayDbPerS);R(decayCoef);R(damperIndex);R(softDamperIndex);
    R(damperT60Seconds);R(softDamperT60Seconds);R(sampleScale);
#undef R
    uint64_t published=0,expected=0;
    if(!read(f,published)||!read(f,expected))return false;
    if(!s.frames||s.frames>maxFramesPerStroke||s.sampleRate<8000||s.sampleRate>384000 ||
       !std::isfinite(s.sampleScale)||s.sampleScale<=0.f||s.sampleScale>10.f||
       s.startFrameOffset>=s.frames||s.alternateStartFrame>=s.frames||
       (s.loop.hasLoop&&(s.loop.start>=s.frames||s.loop.length<2||s.loop.length>s.frames-s.loop.start||
       s.loop.end!=s.loop.start+s.loop.length)))return false;
    const uint64_t bytes=uint64_t(s.frames)*4;
    if(accumulated+bytes>maxPcmBytes)return false;
    accumulated+=bytes;
    s.pcmL.resize(s.frames);s.pcmR.resize(s.frames);
    if(!f.read(reinterpret_cast<char*>(s.pcmL.data()),size_t(s.frames)*2) ||
       !f.read(reinterpret_cast<char*>(s.pcmR.data()),size_t(s.frames)*2))return false;
    if(mix(mix(14695981039346656037ULL,s.pcmL.data(),s.pcmL.size()*2),s.pcmR.data(),s.pcmR.size()*2)!=expected)return false;
    s.readyFrames->store(s.frames,std::memory_order_release);
    s.publishedAutoLoop->store(published,std::memory_order_release);
    return true;
}
void writeInstrument(std::ostream& f,const PianoInstrument& p){
    writeString(f,p.name);writeString(f,p.variant);writeString(f,p.version);writeString(f,p.formatType);
    write(f,p.sf2PresetIndex);write(f,p.isV6);write(f,p.channels);write(f,p.zoneCount);
    write(f,p.memoryMB);write(f,p.releaseMode);write(f,p.undampedFromKey);
    write(f,p.sampleUnison.available);write(f,p.sampleUnison.mode);write(f,p.sampleUnison.topKey);
    for(auto v:p.sampleUnison.detuneRaw)write(f,v);
    for(auto v:p.sampleUnison.pan)write(f,v);
    for(auto v:p.sampleUnison.voiceCounts)write(f,v);
    for(auto v:p.sampleUnison.gainRaw)write(f,v);
    write(f,p.sampleUnison.randomStrokeMode);write(f,p.sampleUnison.blockRandomSustPed);
    write(f,p.sampleVibrato.available);write(f,p.sampleVibrato.enabled);
    for(auto v:p.sampleVibrato.raw)write(f,v);
    for(const auto& pair:p.repeatAlternateKeys)for(auto v:pair)write(f,v);
    for(auto v:p.repeatAlternateCounts)write(f,v);
    write(f,p.headerInfo.isValidCbin);writeString(f,p.headerInfo.signature);
    writeString(f,p.headerInfo.formatCategory);writeString(f,p.headerInfo.versionStr);
    write(f,p.headerInfo.versionRaw);write(f,p.headerInfo.pianoTypeByte);
    writeString(f,p.headerInfo.displayName);writeString(f,p.headerInfo.variant);
}
// v33 compatibility: PCM caches written before Sonus Live Stage store the sample-format
// names under the hardware maker's brand. Translate them to the neutral names used now, so
// the engine's format checks keep working without re-decoding the user's samples.
static void normalizeLegacyFormatName(std::string& s){
    static const std::pair<const char*,const char*> legacy[]={
        {"NORD SAMPLE (","NSMP SAMPLE ("},{"Nord Sample (","Sample NSMP ("},
        {"NORD PIANO (","PIANO NPNO ("},{"Nord Piano v","Piano NPNO v"},{"Nord (","Sample ("}};
    for(const auto& [from,to]:legacy)
        if(s.rfind(from,0)==0){ s=std::string(to)+s.substr(std::char_traits<char>::length(from)); return; }
}

bool readInstrument(std::istream& f,PianoInstrument& p){
    if(!readString(f,p.name)||!readString(f,p.variant)||!readString(f,p.version)||!readString(f,p.formatType)||
       !read(f,p.sf2PresetIndex)||!read(f,p.isV6)||!read(f,p.channels)||!read(f,p.zoneCount)||
       !read(f,p.memoryMB)||!read(f,p.releaseMode)||!read(f,p.undampedFromKey)||
       !read(f,p.sampleUnison.available)||!read(f,p.sampleUnison.mode)||!read(f,p.sampleUnison.topKey))return false;
    for(auto& v:p.sampleUnison.detuneRaw)if(!read(f,v))return false;
    for(auto& v:p.sampleUnison.pan)if(!read(f,v))return false;
    for(auto& v:p.sampleUnison.voiceCounts)if(!read(f,v))return false;
    for(auto& v:p.sampleUnison.gainRaw)if(!read(f,v))return false;
    if(!read(f,p.sampleUnison.randomStrokeMode)||!read(f,p.sampleUnison.blockRandomSustPed)||
       !read(f,p.sampleVibrato.available)||!read(f,p.sampleVibrato.enabled))return false;
    for(auto& v:p.sampleVibrato.raw)if(!read(f,v))return false;
    for(auto& pair:p.repeatAlternateKeys)for(auto& v:pair)if(!read(f,v))return false;
    for(auto& v:p.repeatAlternateCounts)if(!read(f,v))return false;
    if(!read(f,p.headerInfo.isValidCbin)||!readString(f,p.headerInfo.signature)||
       !readString(f,p.headerInfo.formatCategory)||!readString(f,p.headerInfo.versionStr)||
       !read(f,p.headerInfo.versionRaw)||!read(f,p.headerInfo.pianoTypeByte)||
       !readString(f,p.headerInfo.displayName)||!readString(f,p.headerInfo.variant))return false;
    normalizeLegacyFormatName(p.formatType);
    normalizeLegacyFormatName(p.headerInfo.formatCategory);
    return p.zoneCount>=0&&p.zoneCount<=int(maxRecords)&&std::isfinite(p.memoryMB);
}
// v35 trailer after the PCM blocks: the instrument's embedded ENV / filter (SF2 / SFZ).
// Appended rather than bumping cacheVersion, so NSMP caches stay valid; an SF2 / SFZ
// cache written before v35 has no trailer and is decoded once more.
// Layout: magic[8] · payload[26] · FNV-1a checksum of the payload (uint64). Like the PCM
// blocks, a damaged trailer rejects the whole cache (the source is decoded again).
constexpr char embeddedMagic[8]={'N','V','S','E','N','V','0','1'};
std::string embeddedPayload(const PianoInstrument::EmbeddedSound& e){
    std::ostringstream s(std::ios::binary);
    const uint8_t flags=uint8_t((e.available?1:0)|(e.hasFilter?2:0));
    write(s,flags);write(s,uint8_t(0));
    write(s,e.attackSec);write(s,e.decaySec);write(s,e.sustainLevel);write(s,e.releaseSec);
    write(s,e.cutoffHz);write(s,e.resonanceQ);
    return s.str();
}
constexpr size_t embeddedPayloadSize=2+6*sizeof(float);
void writeEmbedded(std::ostream& f,const PianoInstrument::EmbeddedSound& e){
    const auto payload=embeddedPayload(e);
    f.write(embeddedMagic,8);f.write(payload.data(),std::streamsize(payload.size()));
    write(f,mix(14695981039346656037ULL,payload.data(),payload.size()));
}
enum class Trailer { Absent, Valid, Corrupt };
Trailer readEmbedded(std::istream& f,PianoInstrument::EmbeddedSound& e){
    char sign[8]{};
    if(!f.read(sign,8))return Trailer::Absent;                       // pre-v35 cache: nothing after the PCM
    if(std::memcmp(sign,embeddedMagic,8)!=0)return Trailer::Corrupt;
    std::string payload(embeddedPayloadSize,'\0');uint64_t expected=0;
    if(!f.read(&payload[0],std::streamsize(payload.size()))||!read(f,expected))return Trailer::Corrupt;
    if(mix(14695981039346656037ULL,payload.data(),payload.size())!=expected)return Trailer::Corrupt;
    if(f.peek()!=std::char_traits<char>::eof())return Trailer::Corrupt;   // trailing garbage
    std::istringstream s(payload,std::ios::binary);
    uint8_t flags=0,reserved=0;PianoInstrument::EmbeddedSound v;
    read(s,flags);read(s,reserved);
    read(s,v.attackSec);read(s,v.decaySec);read(s,v.sustainLevel);read(s,v.releaseSec);read(s,v.cutoffHz);read(s,v.resonanceQ);
    for(float x:{v.attackSec,v.decaySec,v.sustainLevel,v.releaseSec,v.cutoffHz,v.resonanceQ})if(!std::isfinite(x))return Trailer::Corrupt;
    v.available=(flags&1)!=0;v.hasFilter=(flags&2)!=0;
    e=v;return Trailer::Valid;
}
fs::path uniqueTemp(const fs::path& dest){
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto thread=std::hash<std::thread::id>{}(std::this_thread::get_id());
    return dest.string()+".tmp."+std::to_string(stamp)+"."+std::to_string(thread);
}
}
void DecodedSampleCache::setDirectory(const fs::path& directory){std::lock_guard<std::mutex> g(directoryMutex);chosenDirectory=directory;}
fs::path DecodedSampleCache::directory(){std::lock_guard<std::mutex> g(directoryMutex);return chosenDirectory;}
bool DecodedSampleCache::supports(const std::string& source){
    const auto ext=lower(fs::path(source).extension().string());
    // v35: SF2 is no longer cached. Its PCM16 is stored uncompressed in the bank and the
    // loader memory-maps it, so decoding the chosen preset directly is faster than reading
    // a cache copy (which also doubled the disk use of every SoundFont).
    return ext==".nsmp"||ext==".nsmp3"||ext==".nsmp4"||ext==".sfz";
}
fs::path DecodedSampleCache::cachePath(const std::string& source,int sf2Preset){
    const std::string key=canonical(source)+"#preset="+std::to_string(sf2Preset);
    auto n=stringHash(14695981039346656037ULL,key);
    std::ostringstream name;name<<std::hex<<std::setfill('0')<<std::setw(16)<<n<<".nvscache";
    return directory()/name.str();
}
bool DecodedSampleCache::containsFresh(const std::string& source,int sf2Preset){
    if(!supports(source))return false;
    uint64_t current=0;if(!fingerprint(source,current))return false;
    std::ifstream f(cachePath(source,sf2Preset),std::ios::binary);
    Header hdr{};return read(f,hdr)&&headerValid(hdr,current,sf2Preset);
}
bool DecodedSampleCache::load(const std::string& source,int sf2Preset,PianoInstrument& out){
    if(!supports(source))return false;
    uint64_t current=0;if(!fingerprint(source,current))return false;
    std::ifstream f(cachePath(source,sf2Preset),std::ios::binary);
    Header hdr{};if(!read(f,hdr)||!headerValid(hdr,current,sf2Preset))return false;
    std::error_code ec;const auto diskBytes=fs::file_size(cachePath(source,sf2Preset),ec);
    if(ec||diskBytes<uint64_t(sizeof(Header))+hdr.pcmBytes)return false;
    PianoInstrument stage;
    if(!readInstrument(f,stage))return false;
    stage.strokes.resize(hdr.strokes);
    uint64_t actual=0;
    for(auto& stroke:stage.strokes)if(!readStroke(f,stroke,actual))return false;
    if(actual!=hdr.pcmBytes)return false;
    switch(readEmbedded(f,stage.embeddedSound)){
        case Trailer::Valid:break;
        case Trailer::Corrupt:return false;
        case Trailer::Absent:
            // Pre-v35 cache: an SFZ is decoded again to read its envelope; NSMP has none.
            if(lower(fs::path(source).extension().string())==".sfz")return false;
            stage.embeddedSound={};
            break;
    }
    // Recheck source to avoid racing edits during the cache read.
    uint64_t after=0;if(!fingerprint(source,after)||after!=current)return false;
    stage.sourcePath=canonical(source);
    stage.id=stringHash(14695981039346656037ULL,stage.sourcePath+"#"+std::to_string(sf2Preset));
    out=std::move(stage);return true;
}
bool DecodedSampleCache::store(const std::string& source,int sf2Preset,const PianoInstrument& bank){
    if(!supports(source)||bank.strokes.empty()||bank.strokes.size()>maxRecords)return false;
    uint64_t stamp=0;if(!fingerprint(source,stamp))return false;
    const auto hdr=makeHeader(stamp,sf2Preset,bank);
    if(hdr.pcmBytes>maxPcmBytes)return false;
    for(const auto& s:bank.strokes)
        if(!s.frames||s.pcmL.size()!=s.frames||s.pcmR.size()!=s.frames||
           !s.readyFrames||s.readyFrames->load(std::memory_order_acquire)!=s.frames ||
           s.sampleScale<=0 || !std::isfinite(s.sampleScale))return false;
    std::error_code ec;const auto dest=cachePath(source,sf2Preset);
    fs::create_directories(dest.parent_path(),ec);if(ec)return false;
    const auto temp=uniqueTemp(dest);
    std::ofstream f(temp,std::ios::binary|std::ios::trunc);
    if(!f)return false;
    write(f,hdr);writeInstrument(f,bank);
    for(const auto& s:bank.strokes)writeStroke(f,s);
    writeEmbedded(f,bank.embeddedSound);
    f.flush();const bool success=f.good();f.close();
    uint64_t after=0;
    if(!success||!fingerprint(source,after)||after!=stamp){fs::remove(temp,ec);return false;}
#ifdef _WIN32
    const bool renamed=MoveFileExW(temp.wstring().c_str(),dest.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    fs::rename(temp,dest,ec);const bool renamed=!ec;
#endif
    if(!renamed)fs::remove(temp,ec);
    return renamed;
}
}

namespace SonusDSP {
// Move an already decoded cache when a user explicitly relinks a moved folder.
// The original source must be missing. This is deliberately disabled for SFZ,
// whose nested sample paths are also part of the fingerprint.
bool DecodedSampleCache::rebindRelocated(const std::string& oldSource,
                                        const std::string& newSource,int preset){
    if(lower(fs::path(oldSource).extension().string())==".sfz" ||
       lower(fs::path(oldSource).extension().string())!=lower(fs::path(newSource).extension().string()) ||
       !supports(newSource))return false;
    std::error_code ec;
    if(fs::is_regular_file(oldSource,ec)||!fs::is_regular_file(newSource,ec)||ec)return false;
    uint64_t size=fs::file_size(newSource,ec);if(ec)return false;
    auto time=fs::last_write_time(newSource,ec);if(ec)return false;
    const int64_t stamp=static_cast<int64_t>(time.time_since_epoch().count());
    // v28.1 fingerprint for a single-file source = path + size + mtime.
    auto stampFor=[&](const std::string& path){
        uint64_t h=14695981039346656037ULL;
        h=stringHash(h,canonical(path));h=mix(h,&size,sizeof(size));
        return mix(h,&stamp,sizeof(stamp));
    };
    const auto oldPath=cachePath(oldSource,preset),newPath=cachePath(newSource,preset);
    if(containsFresh(newSource,preset))return true;
    std::ifstream input(oldPath,std::ios::binary);
    Header hdr{};
    if(!read(input,hdr)||!headerValid(hdr,stampFor(oldSource),preset))return false;
    input.close();
    fs::create_directories(newPath.parent_path(),ec);if(ec)return false;
    const auto temp=uniqueTemp(newPath);
    if(!fs::copy_file(oldPath,temp,fs::copy_options::none,ec)||ec)return false;
    {
        std::fstream file(temp,std::ios::in|std::ios::out|std::ios::binary);
        hdr.fingerprint=stampFor(newSource);
        file.write(reinterpret_cast<const char*>(&hdr),sizeof(hdr));file.flush();
        if(!file.good()){file.close();fs::remove(temp,ec);return false;}
    }
    // Publish atomically. On next load, every PCM block is checked independently.
#ifdef _WIN32
    const bool ok=MoveFileExW(temp.wstring().c_str(),newPath.wstring().c_str(),
        MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    fs::rename(temp,newPath,ec);const bool ok=!ec;
#endif
    if(!ok)fs::remove(temp,ec);
    return ok&&containsFresh(newSource,preset);
}
}
