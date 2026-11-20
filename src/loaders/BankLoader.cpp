#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "BankLoader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <map>
#include <thread>
#include <atomic>
#include <chrono>
#include <cctype>
#include <iterator>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include "NsmpLoader.h"
#include "Sf2Loader.h"
#include "ReadOnlyFileBytes.h"

namespace fs = std::filesystem;

namespace SonusDSP {

namespace {

static std::atomic<uint64_t> g_nextInstrumentId{1};

inline uint16_t readU16BE(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

inline int16_t readI16BE(const uint8_t* p) {
    return static_cast<int16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

inline uint32_t readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

constexpr size_t DIR_OFFSET = 0x61c;
constexpr size_t LAYER_TABLE_OFFSET = 0x622;
constexpr size_t RECORDS_OFFSET = 0x732;
constexpr size_t RECORD_SIZE = 0x76;
constexpr int BLOCK_OVERLAP = 64;
constexpr uint16_t CNSP_V6 = 0x464;

const int PREDICTOR_LEN[5] = {0, 1, 2, 3, 4};
const int PREDICTORS[5][4] = {
    {0,  0, 0,  0},
    {1,  0, 0,  0},
    {2, -1, 0,  0},
    {3, -3, 1,  0},
    {4, -6, 4, -1}
};

template<typename Bytes> int findCnspOffset(const Bytes& bytes) {
    size_t limit = std::min<size_t>(bytes.size(), 0x100);
    for (size_t i = 0; i + 4 <= limit; ++i) {
        if (bytes[i] == 0x43 && bytes[i + 1] == 0x4e && bytes[i + 2] == 0x53 && bytes[i + 3] == 0x50) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline int framesPerBlock(int blockWords, int width, int channels) {
    if (width <= 0 || channels <= 0) return 0;
    return (((blockWords - 1) * 16) / (width * channels)) - BLOCK_OVERLAP;
}

std::string extractFirstAsciiRun(const std::vector<uint8_t>& bytes, size_t from, size_t to, size_t minLen = 3) {
    std::string run;
    size_t limit = std::min(to, bytes.size());
    for (size_t i = from; i < limit; ++i) {
        uint8_t c = bytes[i];
        if (c >= 0x20 && c < 0x7f) {
            run.push_back(static_cast<char>(c));
        } else {
            if (run.size() >= minLen) break;
            run.clear();
        }
    }
    while (!run.empty() && (run.back() == ' ' || run.back() == '\t')) run.pop_back();
    while (!run.empty() && (run.front() == ' ' || run.front() == '\t')) run.erase(run.begin());
    return (run.size() >= minLen) ? run : "";
}

} // namespace

// Long, stereo-aware late-tail loop. Never touch a valid factory loop.
// In progressive mode the loop is published atomically instead of changing
// s.loop (which is read by the realtime callback).
void BankLoader::ensureSustainLoop(StrokeSample& s, bool progressive) {
    if (s.loop.hasLoop && s.loop.length >= 4 &&
        s.loop.start < s.frames && s.loop.length <= s.frames-s.loop.start) return;
    if (s.frames < 16384 || s.pcmL.size()<s.frames || s.pcmR.size()<s.frames ||
        s.actualRootMidi>=86 || s.isReleaseStroke || s.explicitOneShot) return;

    const uint32_t searchStart=static_cast<uint32_t>(s.frames*0.76);
    const uint32_t searchEnd=static_cast<uint32_t>(s.frames*0.95);
    if(searchEnd<=searchStart+8192) return;
    auto ascending=[&](uint32_t i)->bool{
        if(i==0||i+1>=s.frames)return false;
        const int l0=s.pcmL[i-1],l1=s.pcmL[i];
        const int r0=s.pcmR[i-1],r1=s.pcmR[i];
        return l0 <= 0 && l1 > 0 &&
            ((r0 <= 0 && r1 > 0) ||
             std::abs(r1-r0) < std::max(20,3*(l1-l0)));
    };
    uint32_t start=0,end=0;
    int firstSlope=0;
    for(uint32_t i=searchStart;i<searchStart+4096&&i<searchEnd;++i){
        if(ascending(i)){start=i;firstSlope=s.pcmL[i]-s.pcmL[i-1];break;}
    }
    if(!start)return;
    int bestScore=std::numeric_limits<int>::max();
    const uint32_t from=std::max(start+8192,searchEnd-4096);
    for(uint32_t i=from;i<searchEnd;++i){
        if(!ascending(i))continue;
        const int slope=s.pcmL[i]-s.pcmL[i-1];
        const int rightSlope=s.pcmR[i]-s.pcmR[i-1];
        const int leftSlope=s.pcmR[start]-s.pcmR[start-1];
        const int score=std::abs(firstSlope-slope)+std::abs(leftSlope-rightSlope);
        if(score<bestScore){bestScore=score;end=i;}
    }
    if(end<=start+8192)return;

    // Analyze a late, stationary stereo tail rather than blindly looping a
    // decaying piano/EP attack. A coherent, shared gain keeps stereo imaging.
    const uint32_t xfade=std::min<uint32_t>(4096,std::min<uint32_t>((end-start)/3,start/3));
    if(xfade<2048)return;
    auto rms=[&](const std::vector<int16_t>& pcm,uint32_t offset,uint32_t len){
        double sum=0.;
        for(uint32_t k=0;k<len;++k){const double x=pcm[offset+k];sum+=x*x;}
        return std::sqrt(sum/double(len));
    };
    const uint32_t probe=std::min<uint32_t>(1024,xfade);
    const double headL=rms(s.pcmL,start-probe,probe),headR=rms(s.pcmR,start-probe,probe);
    const double tailL=rms(s.pcmL,end-probe,probe),tailR=rms(s.pcmR,end-probe,probe);
    const double earlyL=rms(s.pcmL,start-2*probe,probe),earlyR=rms(s.pcmR,start-2*probe,probe);
    const auto stable=[](double a,double b){
        return a>55. && b>55. && std::abs(20.*std::log10(a/b))<1.2;
    };
    if(!stable(headL,tailL)||!stable(headR,tailR)||
       !stable(earlyL,headL)||!stable(earlyR,headR))return;
    // Equal-power fades are not invisible for unrelated spectral/phase states.
    // Accept only tails whose stereo waveforms agree across the actual splice;
    // otherwise preserve the original one-shot instead of creating a pump.
    auto correlation=[&](const std::vector<int16_t>& pcm)->double{
        double xy=0.,xx=0.,yy=0.;
        for(uint32_t k=0;k<xfade;k+=4){
            const double a=pcm[end-xfade+k],b=pcm[start-xfade+k];
            xy+=a*b;xx+=a*a;yy+=b*b;
        }
        return xy/std::sqrt(std::max(1.,xx*yy));
    };
    if(correlation(s.pcmL)<.80 || correlation(s.pcmR)<.80)return;
    // Both channels share ONE RMS correction. Independent channel gain would
    // shift the stereo field each time the sustain repeats.
    const float sharedGain=std::clamp(float(std::sqrt((tailL*tailL+tailR*tailR)/
        std::max(1.,headL*headL+headR*headR))),.93f,1.07f);
    const float inv=float(1.)/float(xfade);
    auto splice=[&](std::vector<int16_t>& pcm){
        for(uint32_t k=0;k<xfade;++k){
            const float u=(float(k)+.5f)*inv;
            const float theta=u*1.5707963267948966f;
            const float a=std::cos(theta),b=std::sin(theta);
            // The gain returns exactly to unity at loopEnd: no level step
            // when the stream immediately continues at the original loopStart.
            const float gain=1.f+(sharedGain-1.f)*(1.f-u)*(1.f-u);
            const float normalizer=1.f/std::max(1.f,a+b);
            const float out=(a*float(pcm[end-xfade+k])+
                             b*gain*float(pcm[start-xfade+k]))*normalizer;
            pcm[end-xfade+k]=int16_t(std::lrint(std::clamp(out,-32768.f,32767.f)));
        }
    };
    splice(s.pcmL);splice(s.pcmR);
    if(progressive){
        s.publishedAutoLoop->store((uint64_t(start)<<32)|end,std::memory_order_release);
    }else{
        s.loop={true,true,start,end-start,end};
    }
    if(!progressive && s.decayCoef>=0.999999f){
        s.loopDecayDbPerS=-4.5f;
        s.decayCoef=std::pow(10.f,-4.5f/(20.f*float(NSP_ENVELOPE_RATE)));
    }
}

ClaviaHeaderInfo BankLoader::inspectClaviaHeader(const std::string& filePath) {
    ClaviaHeaderInfo info;
    std::ifstream f(filePath, std::ios::binary);
    if (!f) return info;

    std::vector<uint8_t> buf(256, 0);
    f.read(reinterpret_cast<char*>(buf.data()), buf.size());
    size_t nRead = static_cast<size_t>(f.gcount());
    if (nRead < 28) return info;
    buf.resize(nRead);

    info.displayName = fs::path(filePath).stem().string();
    if (buf[0] == 'C' && buf[1] == 'B' && buf[2] == 'I' && buf[3] == 'N') {
        info.isValidCbin = true;
        char sig[5] = {static_cast<char>(buf[8]), static_cast<char>(buf[9]),
                       static_cast<char>(buf[10]), static_cast<char>(buf[11]), 0};
        info.signature = std::string(sig);
        info.versionRaw = static_cast<uint32_t>(buf[0x14] | (buf[0x15] << 8));
        uint32_t major = info.versionRaw / 100;
        uint32_t minor = info.versionRaw % 100;
        info.versionStr = std::to_string(major) + "." + (minor < 10 ? "0" : "") + std::to_string(minor);

        int cnsp = findCnspOffset(buf);
        if (cnsp >= 0) {
            if (static_cast<size_t>(cnsp + 0x18) < buf.size()) {
                info.pianoTypeByte = buf[cnsp + 0x18];
            }
            std::string full = extractFirstAsciiRun(buf, cnsp + 0x16, cnsp + 0x16 + 0x67);
            if (!full.empty()) {
                auto hashPos = full.find('#');
                if (hashPos == std::string::npos) {
                    info.displayName = full;
                } else {
                    info.displayName = full.substr(0, hashPos);
                    info.variant = full.substr(hashPos + 1);
                    while (!info.variant.empty() && info.variant.back() == ' ') info.variant.pop_back();
                }
            }
        }

        if (info.signature == "npno") info.formatCategory = "Piano NPNO v" + info.versionStr;
        else if (info.signature == "nsmp" || info.signature == "nsm3" || info.signature == "nsm4")
            info.formatCategory = "Sample NSMP (" + info.signature + ")";
        else
            info.formatCategory = "Sample (" + info.signature + ")";
    }
    return info;
}


namespace {
// Portable field-by-field cache, versioned; no pointers, padding or raw C++ structs.
// Validate source size/mtime AND each region before accepting cached PCM.
// v26: v25 could persist a preload-only normalization scale in NPCACHE4.
// Invalidate it after switching user loads to complete parallel decoding.
constexpr uint32_t NPCACHE_VERSION=5;
std::mutex npnoCacheDirMutex;
fs::path activeNpnoCacheDir=".cache";
fs::path npnoCacheDirectory(){std::lock_guard<std::mutex> guard(npnoCacheDirMutex);return activeNpnoCacheDir;}
template<class T> bool cacheRead(std::ifstream& f,T& v){return bool(f.read(reinterpret_cast<char*>(&v),sizeof(v)));}
template<class T> void cacheWrite(std::ofstream& f,const T& v){f.write(reinterpret_cast<const char*>(&v),sizeof(v));}
fs::path cachePathFor(const std::string& source){
    std::error_code ec;auto normalized=fs::weakly_canonical(source,ec).generic_string();
    if(ec)normalized=fs::path(source).lexically_normal().generic_string();
    uint64_t h=14695981039346656037ULL;
    for(unsigned char c:normalized){h^=c;h*=1099511628211ULL;}
    std::ostringstream name;name<<std::hex<<std::setw(16)<<std::setfill('0')<<h<<".npcache";
    return npnoCacheDirectory()/name.str();
}
bool cacheStamp(const std::string& path,uint64_t& length,int64_t& timestamp){
    std::error_code ec;length=fs::file_size(path,ec);if(ec)return false;
    timestamp=static_cast<int64_t>(fs::last_write_time(path,ec).time_since_epoch().count());
    return !ec;
}
bool loadPcmCache(const std::string& source,std::vector<StrokeSample>& strokes){
    uint64_t size=0;int64_t time=0;if(!cacheStamp(source,size,time))return false;
    std::ifstream f(cachePathFor(source),std::ios::binary);
    if(!f && npnoCacheDirectory()!=fs::path(".cache")){f.open(fs::path(".cache")/cachePathFor(source).filename(),std::ios::binary);}
    if(!f)return false;
    char magic[8]{};if(!f.read(magic,8) || std::string(magic,8)!="NPCACHE5")return false;
    uint32_t version=0,count=0;uint64_t fileSize=0;int64_t fileTime=0;
    if(!cacheRead(f,version)||!cacheRead(f,fileSize)||!cacheRead(f,fileTime)||!cacheRead(f,count)||
       version!=NPCACHE_VERSION||fileSize!=size||fileTime!=time||count!=strokes.size())return false;
    // Staging prevents a truncated cache from publishing a partially loaded bank.
    struct Record{float scale;LoopInfo loop;std::vector<int16_t> left,right;};
    std::vector<Record> records(strokes.size());
    uint64_t cachedBytes=0;
    for(size_t i=0;i<strokes.size();++i){
        auto& r=records[i];uint32_t frames=0;uint8_t loopFlag=0,autoFlag=0;
        uint32_t ls=0,ll=0;
        if(!cacheRead(f,frames)||frames!=strokes[i].frames||frames>100000000 ||
           !cacheRead(f,r.scale)||!std::isfinite(r.scale)||r.scale<=0 ||
           !cacheRead(f,loopFlag)||!cacheRead(f,autoFlag)||
           !cacheRead(f,ls)||!cacheRead(f,ll))return false;
        r.loop={loopFlag!=0,autoFlag!=0,ls,ll,ls+ll};
        if(r.loop.hasLoop && (ls>=frames||ll>frames-ls))return false;
        cachedBytes+=uint64_t(frames)*sizeof(int16_t)*2;
        if(cachedBytes>uint64_t(2)*1024*1024*1024)return false;
        r.left.resize(frames);r.right.resize(frames);
        if(!f.read(reinterpret_cast<char*>(r.left.data()),frames*sizeof(int16_t))||
           !f.read(reinterpret_cast<char*>(r.right.data()),frames*sizeof(int16_t)))return false;
    }
    for(size_t i=0;i<strokes.size();++i){
        auto& s=strokes[i];auto& r=records[i];
        s.sampleScale=r.scale;s.loop=r.loop;
        s.pcmL=std::move(r.left);s.pcmR=std::move(r.right);
        s.readyFrames->store(s.frames,std::memory_order_release);
    }
    return true;
}
void savePcmCache(const std::string& source,const std::vector<StrokeSample>& strokes){
    uint64_t size=0;int64_t time=0;if(!cacheStamp(source,size,time))return;
    std::error_code ec;const auto destination=cachePathFor(source);
    fs::create_directories(destination.parent_path(),ec);if(ec)return;
    // Unique temporary files prevent independent app/plugin processes from
    // corrupting one another's cache writes. The final rename is atomic on
    // POSIX; on Windows replace the destination with MoveFileExW.
    const auto tag=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto tid=std::hash<std::thread::id>{}(std::this_thread::get_id());
    const auto temp=fs::path(destination.string()+".tmp."+std::to_string(tag)+"."+std::to_string(tid));
    std::ofstream f(temp,std::ios::binary|std::ios::trunc);if(!f)return;
    f.write("NPCACHE5",8);cacheWrite(f,NPCACHE_VERSION);cacheWrite(f,size);
    cacheWrite(f,time);const uint32_t count=static_cast<uint32_t>(strokes.size());cacheWrite(f,count);
    for(const auto& s:strokes){
        if(s.frames==0||s.pcmL.size()!=s.frames||s.pcmR.size()!=s.frames||
           s.readyFrames->load(std::memory_order_acquire)!=s.frames){f.close();fs::remove(temp,ec);return;}
        LoopInfo loop=s.loop;
        if(!loop.hasLoop && s.publishedAutoLoop){
            const uint64_t packed=s.publishedAutoLoop->load(std::memory_order_acquire);
            if(packed){loop.start=uint32_t(packed>>32);loop.end=uint32_t(packed);
                loop.length=loop.end-loop.start;loop.hasLoop=true;loop.isAutoLoop=true;}
        }
        cacheWrite(f,s.frames);cacheWrite(f,s.sampleScale);
        const uint8_t valid=loop.hasLoop?1:0,autoLoop=loop.isAutoLoop?1:0;
        cacheWrite(f,valid);cacheWrite(f,autoLoop);cacheWrite(f,loop.start);cacheWrite(f,loop.length);
        f.write(reinterpret_cast<const char*>(s.pcmL.data()),s.frames*sizeof(int16_t));
        f.write(reinterpret_cast<const char*>(s.pcmR.data()),s.frames*sizeof(int16_t));
    }
    f.flush();const bool ok=f.good();f.close();
    if(!ok){fs::remove(temp,ec);return;}
    // Never publish a cache computed for a source that changed during write.
    uint64_t finalSize=0;int64_t finalTime=0;
    if(!cacheStamp(source,finalSize,finalTime) || finalSize!=size || finalTime!=time){
        fs::remove(temp,ec);return;
    }
#ifdef _WIN32
    const bool replaced=MoveFileExW(temp.c_str(),destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!replaced)fs::remove(temp,ec);
#else
    fs::rename(temp,destination,ec);
    if(ec)fs::remove(temp,ec);
#endif
}
} // anonymous namespace

void BankLoader::setNpnoCacheDirectory(const std::filesystem::path& dir){
    if(!dir.empty()){std::lock_guard<std::mutex> guard(npnoCacheDirMutex);activeNpnoCacheDir=dir;}
}

bool BankLoader::hasNpnoCache(const std::string& source){
    uint64_t size=0;int64_t stamp=0;
    if(!cacheStamp(source,size,stamp))return false;
    // "Prepared" means present in the configured SSD directory. The loader
    // still reads a legacy .cache as a compatibility fallback, but the batch
    // preparation then migrates its PCM into the chosen persistent location.
    std::ifstream f(cachePathFor(source),std::ios::binary);
    char sign[8]{};uint32_t version=0,count=0;uint64_t fileSize=0;int64_t fileTime=0;
    return bool(f.read(sign,8)) && std::string(sign,8)=="NPCACHE5" &&
       cacheRead(f,version) && cacheRead(f,fileSize) && cacheRead(f,fileTime) && cacheRead(f,count) &&
       version==NPCACHE_VERSION && fileSize==size && fileTime==stamp && count>0 && count<=100000;
}

bool BankLoader::rebindNpnoCache(const std::string& oldSource,const std::string& newSource){
    // An explicit user relink can reuse NPCACHE5 if a moved original retains
    // its exact size and modification time. No decoded sample is changed.
    std::error_code ec;
    if(fs::is_regular_file(oldSource,ec)||!fs::is_regular_file(newSource,ec)||ec)return false;
    if(hasNpnoCache(newSource))return true;
    uint64_t newSize=0;int64_t newTime=0;
    if(!cacheStamp(newSource,newSize,newTime))return false;
    const auto oldCache=cachePathFor(oldSource),newCache=cachePathFor(newSource);
    std::ifstream input(oldCache,std::ios::binary);
    if(!input && npnoCacheDirectory()!=fs::path(".cache"))
        input.open(fs::path(".cache")/oldCache.filename(),std::ios::binary);
    char magic[8]{};uint32_t version=0,count=0;uint64_t size=0;int64_t time=0;
    if(!input.read(magic,8)||std::string(magic,8)!="NPCACHE5"||
       !cacheRead(input,version)||!cacheRead(input,size)||!cacheRead(input,time)||
       !cacheRead(input,count)||version!=NPCACHE_VERSION||size!=newSize||
       time!=newTime||count==0||count>100000)return false;
    input.close();
    fs::create_directories(newCache.parent_path(),ec);if(ec)return false;
    const auto temp=fs::path(newCache.string()+".relink."+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    // Prefer a copy: an older program or instance may still use the previous path.
    const auto src=fs::exists(oldCache)?oldCache:fs::path(".cache")/oldCache.filename();
    if(!fs::copy_file(src,temp,fs::copy_options::none,ec)||ec)return false;
#ifdef _WIN32
    const bool ok=MoveFileExW(temp.wstring().c_str(),newCache.wstring().c_str(),
        MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    fs::rename(temp,newCache,ec);const bool ok=!ec;
#endif
    if(!ok)fs::remove(temp,ec);
    return ok&&hasNpnoCache(newSource);
}

bool BankLoader::loadNpnoNative(const std::string& filePath,PianoInstrument& out,
                                std::string& msg,bool skipCacheWrite){
    return loadNpnoNativeImpl(filePath,out,msg,{},skipCacheWrite);
}
void BankLoader::persistNpnoCache(const std::string& filePath,const PianoInstrument& bank){
    savePcmCache(filePath,bank.strokes);
}
bool BankLoader::loadNpnoNativeShared(const std::string& filePath,
         const std::shared_ptr<PianoInstrument>& owner,std::string& msg){
    if(!owner){msg="PianoInstrument nulo";return false;}
    return loadNpnoNativeImpl(filePath,*owner,msg,owner,false);
}
bool BankLoader::loadNpnoNativeImpl(const std::string& filePath, PianoInstrument& outInstrument,
                                     std::string& outStatusMsg,std::shared_ptr<PianoInstrument> owner,
                                     bool skipCacheWrite) {
    auto t0 = std::chrono::steady_clock::now();

    // Memory mapping avoids copying large compressed CNSP banks to a second
    // 80+ MB heap buffer. The mapped view is captured by background decoding
    // workers, so its lifetime always extends until the last reader exits.
    auto sharedBytes = std::make_shared<ReadOnlyFileBytes>(filePath);
    if (!sharedBytes->good()) {
        outStatusMsg = "No se pudo leer: " + filePath;
        return false;
    }
    const auto& bytes = *sharedBytes;
    if (bytes.size() < 0x800) {
        outStatusMsg = "Archivo demasiado pequeño para ser CNSP.";
        return false;
    }

    int base = findCnspOffset(bytes);
    if (base < 0 || static_cast<size_t>(base) + RECORDS_OFFSET > bytes.size()) {
        outStatusMsg = "El archivo no contiene cabecera CNSP.";
        return false;
    }

    uint16_t version = readU16BE(&bytes[base + DIR_OFFSET]);
    uint16_t channels = readU16BE(&bytes[base + DIR_OFFSET + 2]);
    uint16_t strokeCount = readU16BE(&bytes[base + DIR_OFFSET + 4]);

    if ((channels != 1 && channels != 2) || strokeCount == 0 ||
        static_cast<size_t>(base) + RECORDS_OFFSET + strokeCount * RECORD_SIZE > bytes.size()) {
        outStatusMsg = "Directorio CNSP fuera de rango.";
        return false;
    }

    ClaviaHeaderInfo hdr = inspectClaviaHeader(filePath);
    outInstrument.id = g_nextInstrumentId.fetch_add(1);
    outInstrument.headerInfo = hdr;
    outInstrument.name = hdr.displayName.empty() ? fs::path(filePath).stem().string() : hdr.displayName;
    if (!hdr.variant.empty()) outInstrument.name += " [" + hdr.variant + "]";
    outInstrument.variant = hdr.variant;
    outInstrument.version = hdr.versionStr;
    outInstrument.formatType = hdr.formatCategory.empty() ? "PIANO NPNO (.npno)" : hdr.formatCategory;
    outInstrument.sourcePath = filePath;
    outInstrument.isV6 = (version >= CNSP_V6);
    outInstrument.channels = channels;
    outInstrument.releaseMode = bytes[base + 0x49c];
    outInstrument.undampedFromKey = (bytes[base + 0x40d] >= 89) ? bytes[base + 0x40d] : 90;

    // 1. Leer el Keymap en base + 0x8C (readKeyMap de nsp.ts)
    std::map<int, std::pair<int, int>> rootSpans;
    for (int note = 0; note < 128; ++note) {
        uint8_t r = bytes[base + 0x8c + note];
        if (r == 0 || r == 0xff || r > 127) continue;
        auto it = rootSpans.find(r);
        if (it == rootSpans.end()) rootSpans[r] = {note, note};
        else {
            if (note < it->second.first) it->second.first = note;
            if (note > it->second.second) it->second.second = note;
        }
    }

    std::vector<int> keymapRootsSorted;
    for (const auto& [rNote, _] : rootSpans) keymapRootsSorted.push_back(rNote);
    std::sort(keymapRootsSorted.begin(), keymapRootsSorted.end());

    // 2. Leer la tabla layers-per-root: u16BE[128] en base + 0x622 (readLayerTable de nsp-audio.ts)
    std::vector<int> layerTable(128, 0);
    std::vector<int> rootsWithLayers;
    int layerTableSum = 0;
    for (int n = 0; n < 128; ++n) {
        int c = readU16BE(&bytes[base + LAYER_TABLE_OFFSET + n * 2]);
        layerTable[n] = c;
        layerTableSum += c;
        if (c > 0) rootsWithLayers.push_back(n);
    }
    if (rootsWithLayers.empty()) rootsWithLayers = keymapRootsSorted;

    struct RawStrokeDesc {
        int index = 0;
        int zone = 0;
        int storedRoot = 60;
        uint32_t offset = 0;
        int velocityFrom = 0;
        uint8_t flags = 0;
        bool release = false;
        uint32_t frames = 0;
        uint16_t blocks = 0;
        int16_t history[2][4] = {{0}};
        bool hasLoop = false;
        uint32_t loopStart = 0;
        uint32_t loopLength = 0;
        float loopDecay = 1.0f;
        int8_t fineTune = 0;
        uint16_t levelWord = 125;
        uint8_t damperIdx = 44;
        uint8_t softDamperIdx = 50;
    };

    const int floorScale = (version < CNSP_V6) ? 5 : 1;
    std::vector<RawStrokeDesc> rawStrokes(strokeCount);

    for (uint16_t i = 0; i < strokeCount; ++i) {
        size_t at = static_cast<size_t>(base) + RECORDS_OFFSET + i * RECORD_SIZE;
        RawStrokeDesc& rs = rawStrokes[i];
        rs.index = i;
        rs.offset = static_cast<uint32_t>(base) + readU32BE(&bytes[at + 0x00]);
        rs.flags = bytes[at + 0x04] & 3;
        rs.release = (rs.flags == 2);
        rs.velocityFrom = static_cast<int>(bytes[at + 0x05]) * floorScale;
        rs.frames = readU32BE(&bytes[at + 0x06]);
        rs.blocks = readU16BE(&bytes[at + 0x0a]);

        for (uint16_t c = 0; c < channels; ++c) {
            size_t hOff = at + (c == 0 ? 0x0c : 0x14);
            int16_t h0 = readI16BE(&bytes[hOff + 0]);
            int16_t h1 = readI16BE(&bytes[hOff + 2]);
            int16_t h2 = readI16BE(&bytes[hOff + 4]);
            int16_t h3 = readI16BE(&bytes[hOff + 6]);
            rs.history[c][0] = h3;
            rs.history[c][1] = h2;
            rs.history[c][2] = h1;
            rs.history[c][3] = h0;
        }

        rs.loopStart = readU32BE(&bytes[at + 0x1c]);
        rs.loopLength = readU32BE(&bytes[at + 0x20]);
        rs.hasLoop = (rs.loopLength >= 4);
        if (rs.hasLoop) {
            uint32_t q23 = readU32BE(&bytes[at + 0x2e]);
            rs.loopDecay = (q23 > 0 && q23 <= 0x800000)
                ? static_cast<float>(static_cast<double>(q23) / static_cast<double>(0x800000))
                : 0.999985f;
        } else {
            rs.loopDecay = 1.0f;
        }

        rs.fineTune = static_cast<int8_t>(bytes[at + 0x32]);
        rs.levelWord = readU16BE(&bytes[at + 0x34]);
        rs.damperIdx = bytes[at + 0x72];
        rs.softDamperIdx = bytes[at + 0x73];
    }

    // Asignación de zonas según zoneOf() en nsp-audio.ts
    if (layerTableSum == static_cast<int>(strokeCount)) {
        size_t sIdx = 0;
        for (size_t z = 0; z < rootsWithLayers.size(); ++z) {
            int rNote = rootsWithLayers[z];
            int countForRoot = layerTable[rNote];
            for (int k = 0; k < countForRoot && sIdx < rawStrokes.size(); ++k, ++sIdx) {
                rawStrokes[sIdx].zone = static_cast<int>(z);
                rawStrokes[sIdx].storedRoot = rNote;
            }
        }
    } else {
        int z = -1;
        for (auto& rs : rawStrokes) {
            if ((rs.velocityFrom == 0 && !rs.release) || z < 0) ++z;
            rs.zone = z;
            if (z >= 0 && z < static_cast<int>(rootsWithLayers.size())) {
                rs.storedRoot = rootsWithLayers[z];
            } else if (!keymapRootsSorted.empty()) {
                int clampedZ = std::min<int>(z, static_cast<int>(keymapRootsSorted.size()) - 1);
                rs.storedRoot = keymapRootsSorted[clampedZ];
            }
        }
    }

    // En nsp-check.ts y nsp-write.ts blockWords es siempre 511 * channels
    const int blockWords = 511 * channels;

    std::vector<RawStrokeDesc> validStrokes;
    validStrokes.reserve(rawStrokes.size());
    for (const auto& rs : rawStrokes) {
        if (rs.flags > 2) continue; // normal, alternate, and key-release strokes
        if (rs.frames == 0 || rs.blocks == 0) continue;
        if (static_cast<size_t>(rs.offset) + static_cast<size_t>(rs.blocks) * blockWords * 2 > bytes.size()) continue;
        validStrokes.push_back(rs);
    }

    if (validStrokes.empty()) {
        outStatusMsg = "No se encontraron capas válidas en el archivo .npno.";
        return false;
    }

    std::map<int, std::vector<size_t>> strokesByZone;
    for (size_t i = 0; i < validStrokes.size(); ++i) {
        if(!validStrokes[i].release && validStrokes[i].flags != 1)
            strokesByZone[validStrokes[i].zone].push_back(i);
    }

    outInstrument.zoneCount = static_cast<int>(strokesByZone.size());
    outInstrument.strokes.clear();
    outInstrument.strokes.resize(validStrokes.size());

    for (auto& [zoneId, indices] : strokesByZone) {
        int totalL = static_cast<int>(indices.size());
        for (int lIdx = 0; lIdx < totalL; ++lIdx) {
            size_t i = indices[lIdx];
            const RawStrokeDesc& rs = validStrokes[i];
            StrokeSample& s = outInstrument.strokes[i];

            s.index = rs.index;
            s.zone = rs.zone;
            s.layerIndex = lIdx;
            s.totalLayersInZone = totalL;
            s.velocityFrom = rs.velocityFrom;
            if (lIdx == 0) {
                s.switchPoint = 0;
            } else {
                int prevFloor = validStrokes[indices[lIdx - 1]].velocityFrom;
                s.switchPoint = rs.velocityFrom - ((rs.velocityFrom - prevFloor) / 2);
            }

            s.storedRoot = rs.storedRoot;
            s.actualRootMidi = rs.storedRoot + NSP_ROOT_OFFSET;

            auto spanIt = rootSpans.find(rs.storedRoot);
            if (spanIt != rootSpans.end()) {
                s.lokey = spanIt->second.first;
                s.hikey = spanIt->second.second;
            } else {
                s.lokey = std::max(0, s.actualRootMidi - 2);
                s.hikey = std::min(127, s.actualRootMidi + 1);
            }

            // Calcular el rango equivalente lovel..hivel según la curva de atenuación del formato
            int softIdx = totalL - 1 - lIdx;
            s.lovel = (softIdx == 0) ? 1 : (softIdx * 127) / totalL + 1;
            s.hivel = (softIdx == totalL - 1) ? 127 : ((softIdx + 1) * 127) / totalL;

            s.frames = rs.frames;
            s.sampleRate = static_cast<uint32_t>(NSP_SAMPLE_RATE);
            const bool validFactory=rs.hasLoop && rs.loopLength>=4 &&
                rs.loopStart<rs.frames && rs.loopLength<=rs.frames-rs.loopStart;
            s.loop={validFactory,false,validFactory?rs.loopStart:0u,
                validFactory?rs.loopLength:0u,validFactory?rs.loopStart+rs.loopLength:0u};
            s.decayCoef = rs.loopDecay;
            s.loopDecayDbPerS = rs.hasLoop ? (20.0f * std::log10(std::max(1e-7f, rs.loopDecay)) * static_cast<float>(NSP_ENVELOPE_RATE)) : -3.5f;

            s.fineTuneCents = static_cast<float>(rs.fineTune);
            int8_t stretchByte = static_cast<int8_t>(bytes[base + 0x18c + std::clamp(s.hikey, 0, 127)]);
            s.stretchCents = static_cast<float>((outInstrument.isV6 ? 1 : -1) * stretchByte - rs.fineTune);

            s.baseLevelWord = rs.levelWord;
            // Atenuación suave sin multiplicar por encima de 1.0f para evitar cualquier distorsión
            if (rs.levelWord > 125 && rs.levelWord < 600) {
                float attDb = static_cast<float>(rs.levelWord - 125) * 0.08f;
                s.levelGainLinear = std::clamp(std::pow(10.0f, -attDb / 20.0f), 0.25f, 1.0f);
            } else {
                s.levelGainLinear = 1.0f;
            }

            s.damperIndex = rs.damperIdx ? rs.damperIdx : 44;
            s.softDamperIndex = rs.softDamperIdx ? rs.softDamperIdx : (s.damperIndex + 6);
            s.damperT60Seconds = 0.0015f * std::pow(10.0f, s.damperIndex / 20.0f);
            s.softDamperT60Seconds = 0.0015f * std::pow(10.0f, s.softDamperIndex / 20.0f);
        }
    }

    // Auxiliary recordings are not normal velocity layers. Keep their decoded
    // samples in this instrument, but never let them compete with the main keymap.
    for(size_t i=0;i<validStrokes.size();++i){
        auto& rs=validStrokes[i];
        if(rs.flags!=1 && !rs.release)continue;
        auto& s=outInstrument.strokes[i];
        s.index=rs.index;s.zone=rs.zone;s.flags=rs.flags;
        s.isReleaseStroke=rs.release;
        s.storedRoot=rs.storedRoot;s.actualRootMidi=rs.storedRoot+NSP_ROOT_OFFSET;
        s.frames=rs.frames;s.sampleRate=uint32_t(NSP_SAMPLE_RATE);
        s.velocityFrom=rs.velocityFrom;s.switchPoint=rs.velocityFrom;
        s.lovel=1;s.hivel=127;
        auto span=rootSpans.find(rs.storedRoot);
        if(span!=rootSpans.end()){
            s.lokey=span->second.first;s.hikey=span->second.second;
        }else{s.lokey=std::max(0,s.actualRootMidi-2);s.hikey=std::min(127,s.actualRootMidi+2);}
        const bool valid=rs.hasLoop && rs.loopLength>=4 && rs.loopStart<rs.frames &&
            rs.loopLength<=rs.frames-rs.loopStart && !rs.release;
        s.loop={valid,false,valid?rs.loopStart:0u,valid?rs.loopLength:0u,
            valid?rs.loopStart+rs.loopLength:0u};
        s.explicitOneShot=rs.release;
        s.fineTuneCents=float(rs.fineTune);
        s.baseLevelWord=rs.levelWord;
        if(rs.levelWord>125 && rs.levelWord<600)
            s.levelGainLinear=std::clamp(std::pow(10.f,-float(rs.levelWord-125)*.08f/20.f),.25f,1.f);
        s.decayCoef=rs.loopDecay;
        s.loopDecayDbPerS=valid?20.f*std::log10(std::max(1e-7f,rs.loopDecay))*float(NSP_ENVELOPE_RATE):0.f;
        s.damperIndex=rs.damperIdx?rs.damperIdx:44;
        s.softDamperIndex=rs.softDamperIdx?rs.softDamperIdx:50;
        s.damperT60Seconds=.0015f*std::pow(10.f,s.damperIndex/20.f);
        s.softDamperT60Seconds=.0015f*std::pow(10.f,s.softDamperIndex/20.f);
    }

    if (loadPcmCache(filePath,outInstrument.strokes)) {
        outInstrument.memoryMB=0;
        for(const auto& st:outInstrument.strokes)
            outInstrument.memoryMB+=(st.pcmL.size()+st.pcmR.size())*sizeof(int16_t)/(1024.0*1024.0);
        outStatusMsg="Desde caché PCM16: "+outInstrument.name;
        return true;
    }

    struct DecodeState {
        int32_t hist[2][4]{};
        uint32_t written=0;
        uint16_t block=0;
        int32_t maxPeak=1;
        bool valid=true;
    };
    std::vector<DecodeState> states(validStrokes.size());
    for(size_t idx=0;idx<validStrokes.size();++idx){
        const auto& rs=validStrokes[idx];auto& st=states[idx];
        for(int c=0;c<2;++c)for(int k=0;k<4;++k)st.hist[c][k]=rs.history[c][k];
    }
    auto decodeStroke=[&](size_t idx,uint16_t maxBlocks){
        const auto& rs=validStrokes[idx];auto& st=states[idx];
        auto& dst=outInstrument.strokes[idx];
        // Each stroke is owned by exactly one worker. Allocate and decode it
        // on that worker instead of serially zeroing hundreds of PCM vectors.
        if (st.block==0 && st.written==0) {
            dst.pcmL.resize(rs.frames);dst.pcmR.resize(rs.frames);
        }
        for(;st.block<maxBlocks && st.written<rs.frames && st.valid;++st.block){
            const size_t at=size_t(rs.offset)+size_t(st.block)*size_t(blockWords)*2;
            if(at+size_t(blockWords)*2>bytes.size()){st.valid=false;break;}
            const uint16_t header=readU16BE(&bytes[at]);
            const int width=header&0x1f,order=(header>>5)&7;
            if(width==0||width>24||order>=5){st.valid=false;break;}
            const int* coef=PREDICTORS[order];const int coefLen=PREDICTOR_LEN[order];
            const int own=framesPerBlock(blockWords,width,channels);
            if(own<=0){st.valid=false;break;}
            const int32_t half=1<<(width-1),full=1<<width;
            const uint8_t* bitNext=bytes.data()+at+2;
            const uint8_t* bitEnd=bytes.data()+at+size_t(blockWords)*2;
            uint64_t reservoir=0;
            unsigned available=0;
            for(int n=0;n<own&&st.written<rs.frames;++n){
                int32_t frame[2]={0,0};
                for(uint16_t c=0;c<channels;++c){
                    // CNSP stores LSB-first bit fields within BE 16-bit words.
                    // A 64-bit reservoir avoids reloading the same word on
                    // every 3-8-bit sample while retaining exact integer PCM.
                    while(available < unsigned(width)) {
                        if(size_t(bitEnd-bitNext)<2){st.valid=false;break;}
                        reservoir |= uint64_t(readU16BE(bitNext)) << available;
                        bitNext+=2;available+=16;
                    }
                    if(!st.valid)break;
                    const uint32_t v=uint32_t(reservoir)&((1u<<width)-1u);
                    reservoir >>= width;
                    available -= unsigned(width);
                    const int32_t residual=int32_t(v)>=half?int32_t(v)-full:int32_t(v);
                    int64_t x=residual;
                    for(int k=0;k<coefLen;++k)x+=int64_t(coef[k])*st.hist[c][k];
                    // Predictor history must retain the full reconstructed
                    // value. Only the PCM16 storage representation is clipped.
                    // Clipping history would corrupt subsequent delta samples.
                    const int32_t predictorSample=static_cast<int32_t>(
                        std::clamp<int64_t>(x,INT32_MIN,INT32_MAX));
                    for(int k=3;k>0;--k)st.hist[c][k]=st.hist[c][k-1];
                    st.hist[c][0]=predictorSample;
                    frame[c]=std::clamp(predictorSample,-32768,32767);
                    st.maxPeak=std::max(st.maxPeak,std::abs(frame[c]));
                }
                if(!st.valid)break;
                dst.pcmL[st.written]=static_cast<int16_t>(frame[0]);
                dst.pcmR[st.written]=static_cast<int16_t>(channels==2?frame[1]:frame[0]);
                ++st.written;
            }
        }
        // Lock-free producer/consumer release-publish; never advertise the
        // late tail prior to final splice for factory-loop-less material.
        const bool autoCandidate=!dst.loop.hasLoop&&!dst.explicitOneShot&&!dst.isReleaseStroke&&dst.actualRootMidi<86;
        const uint32_t prefix=(owner && autoCandidate && maxBlocks>=rs.blocks)
            ? std::min<uint32_t>(st.written,uint32_t(rs.frames*0.72)) : st.written;
        dst.readyFrames->store(prefix,std::memory_order_release);
    };
    auto decodeStage=[&](bool preload){
        // Work stealing scales on modern 8/16-core hosts without occupying
        // every hardware thread. Keep one logical CPU for the real-time audio
        // callback and other devices while decoding on a control thread.
        const unsigned hardware=std::max(1u,std::thread::hardware_concurrency());
        const unsigned available=hardware>2?hardware-1:hardware;
        const unsigned threads=std::min<unsigned>(std::min(available,8u),
                                            static_cast<unsigned>(validStrokes.size()));
        std::atomic<size_t> next{0};std::vector<std::thread> workers;workers.reserve(threads);
        for(unsigned th=0;th<threads;++th)workers.emplace_back([&]{
            for(;;){const size_t idx=next.fetch_add(1);if(idx>=states.size())break;
                decodeStroke(idx,preload?std::min<uint16_t>(6,validStrokes[idx].blocks):validStrokes[idx].blocks);
            }
        });
        for(auto& worker:workers)worker.join();
    };

    // Phase 1: only first six blocks per region; playback may begin now.
    decodeStage(bool(owner));
    for(size_t idx=0;idx<states.size();++idx){
        if(!states[idx].valid || states[idx].written<4){
            outStatusMsg="Error en el ataque CNSP: stroke "+std::to_string(idx);
            return false;
        }
        const float peak=float(states[idx].maxPeak);
        outInstrument.strokes[idx].sampleScale=1.f/std::max(12000.f,peak*1.18f);
    }
    if(owner){
        // Background owns all resources; PCM vectors are already fully sized,
        // and only contiguous readyFrames is published after writes.
        std::thread([owner,sharedBytes,states=std::move(states),strokes=std::move(validStrokes),
                     blockWords,channels,filePath]() mutable {
            const auto& bytes=*sharedBytes;
            for(size_t idx=0;idx<states.size();++idx){
                auto& st=states[idx];auto& dst=owner->strokes[idx];const auto& rs=strokes[idx];
                for(;st.block<rs.blocks && st.written<rs.frames && st.valid;++st.block){
                    const size_t at=size_t(rs.offset)+size_t(st.block)*size_t(blockWords)*2;
                    if(at+size_t(blockWords)*2>bytes.size()){st.valid=false;break;}
                    const uint16_t hdr=readU16BE(&bytes[at]);
                    const int width=hdr&31,order=(hdr>>5)&7;
                    if(width==0||width>24||order>=5){st.valid=false;break;}
                    const int own=framesPerBlock(blockWords,width,channels);
                    if(own<=0){st.valid=false;break;}
                    const int32_t half=1<<(width-1),full=1<<width;
                    const uint8_t* bitNext=bytes.data()+at+2;
                    const uint8_t* bitEnd=bytes.data()+at+size_t(blockWords)*2;
                    uint64_t reservoir=0;unsigned available=0;
                    for(int n=0;n<own && st.written<rs.frames;++n){
                        int32_t frame[2]={0,0};
                        for(uint16_t c=0;c<channels;++c){
                            while(available<unsigned(width)){
                                if(size_t(bitEnd-bitNext)<2){st.valid=false;break;}
                                reservoir |= uint64_t(readU16BE(bitNext))<<available;
                                bitNext+=2;available+=16;
                            }
                            if(!st.valid)break;
                            const uint32_t v=uint32_t(reservoir)&((1u<<width)-1u);
                            reservoir>>=width;available-=unsigned(width);
                            int64_t x=int32_t(v)>=half?int32_t(v)-full:int32_t(v);
                            for(int k=0;k<PREDICTOR_LEN[order];++k)x+=int64_t(PREDICTORS[order][k])*st.hist[c][k];
                            const int32_t predictorSample=static_cast<int32_t>(
                                std::clamp<int64_t>(x,INT32_MIN,INT32_MAX));
                            for(int k=3;k>0;--k)st.hist[c][k]=st.hist[c][k-1];
                            st.hist[c][0]=predictorSample;
                            frame[c]=std::clamp(predictorSample,-32768,32767);
                        }
                        if(!st.valid)break;
                        dst.pcmL[st.written]=int16_t(frame[0]);
                        dst.pcmR[st.written]=int16_t(channels==2?frame[1]:frame[0]);
                        ++st.written;
                    }
                    const bool autoCandidate=!dst.loop.hasLoop&&!dst.explicitOneShot&&!dst.isReleaseStroke&&dst.actualRootMidi<86;
                    const uint32_t prefix=autoCandidate?
                        std::min<uint32_t>(st.written,uint32_t(rs.frames*0.72)):st.written;
                    dst.readyFrames->store(prefix,std::memory_order_release);
                }
                if(st.valid&&st.written==rs.frames){
                    BankLoader::ensureSustainLoop(dst,true);
                    dst.readyFrames->store(rs.frames,std::memory_order_release);
                }
            }
            savePcmCache(filePath,owner->strokes);
        }).detach();
    }else{
        for(size_t idx=0;idx<states.size();++idx){
            auto& st=states[idx];auto& dst=outInstrument.strokes[idx];
            if(!st.valid||st.written<16){dst.readyFrames->store(0);continue;}
            dst.frames=st.written;
            dst.sampleScale=1.f/std::max(12000.f,float(st.maxPeak)*1.18f);
            dst.pcmL.resize(dst.frames);dst.pcmR.resize(dst.frames);
            ensureSustainLoop(dst);
            dst.readyFrames->store(dst.frames,std::memory_order_release);
        }
        outInstrument.strokes.erase(std::remove_if(outInstrument.strokes.begin(),outInstrument.strokes.end(),
            [](const StrokeSample& st){return st.readyFrames->load()==0;}),outInstrument.strokes.end());
        if(!skipCacheWrite)savePcmCache(filePath,outInstrument.strokes);
    }

    size_t totalFloats = 0;
    for (const auto& s : outInstrument.strokes) totalFloats += s.pcmL.size() + s.pcmR.size();
    outInstrument.memoryMB = static_cast<double>(totalFloats * sizeof(int16_t)) / (1024.0 * 1024.0);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    outStatusMsg = "Cargado: " + outInstrument.name + " (" +
                   std::to_string(outInstrument.strokes.size()) + " muestras, " +
                   std::to_string(outInstrument.zoneCount) + " zonas en " + std::to_string(ms) + " ms)";
    std::cout << "[CNSP Native] " << outStatusMsg << "\n";
    return !outInstrument.strokes.empty();
}

// Soporte para archivos .nsmp / .nsmp3 / .nsmp4 para que seleccionarlos en la lista cambie efectivamente de instrumento
// ============================================================================
//  Decodificador Nativo C++ para .nsmp, .nsmp3 y .nsmp4 (Códec NW1)
//  Traducido directamente de nsmp-codec.ts y nsmp.ts
// ============================================================================
namespace {

inline uint32_t readU24BE_Nsmp(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8)  |
            static_cast<uint32_t>(p[2]);
}

const int NW1_PRED_LEN[8] = {0, 1, 2, 3, 4, 5, 6, 7};
const int NW1_PRED_COEFF[8][7] = {
    {0,   0,   0,   0,  0,  0, 0},
    {1,   0,   0,   0,  0,  0, 0},
    {2,  -1,   0,   0,  0,  0, 0},
    {3,  -3,   1,   0,  0,  0, 0},
    {4,  -6,   4,  -1,  0,  0, 0},
    {5, -10,  10,  -5,  1,  0, 0},
    {6, -15,  20, -15,  6, -1, 0},
    {7, -21,  35, -35, 21, -7, 1}
};

struct NsmpSectionInfo {
    std::string tag;
    uint32_t version = 0;
    uint32_t size = 0;
    size_t payloadOffset = 0;
    size_t endOffset = 0;
};

struct NsmpMark {
    uint32_t word = 0;
    uint32_t sample = 0;
    bool loopDecay = false;
};

struct DecodedNw1Stroke {
    std::vector<int32_t> chL;
    std::vector<int32_t> chR;
    int channelCount = 1;
    size_t endOffset = 0;
    std::vector<NsmpMark> marks;
};

inline int32_t signExtendNw1(uint32_t v, int bw) {
    uint32_t half = 1u << (bw - 1);
    return (v >= half) ? static_cast<int32_t>(v) - static_cast<int32_t>(1u << bw) : static_cast<int32_t>(v);
}

bool decodeNw1Stream(const std::vector<uint8_t>& bytes, size_t byteOffset, size_t sectionEnd,
                     int channelCount, bool wordInterleaved, bool u24, DecodedNw1Stroke& out) {
    const size_t hdrBytes  = u24 ? 3 : 4;
    const size_t wordBytes = u24 ? 3 : 4;
    const int wordBits     = u24 ? 24 : 32;

    struct ChState {
        int32_t ring[32] = {0};
        int head = 0;
        std::vector<int32_t> samples;
    };
    ChState chs[2];
    chs[0].samples.reserve(65536);
    if (channelCount > 1) chs[1].samples.reserve(65536);

    auto emitSample = [&](int c, int32_t residual, int order) -> bool {
        ChState& cs = chs[c];
        int64_t pred = 0;
        const int* coeff = NW1_PRED_COEFF[order];
        for (int k = 0; k < order; ++k) {
            pred += static_cast<int64_t>(coeff[k]) * cs.ring[(cs.head - 1 - k) & 31];
        }
        int64_t val = static_cast<int64_t>(residual) + pred;
        if (std::abs(val) >= (1LL << 26)) return false; // Límite BOUND (1 << 26) de nsmp.ts
        cs.ring[cs.head] = static_cast<int32_t>(val);
        cs.head = (cs.head + 1) & 31;
        cs.samples.push_back(static_cast<int32_t>(val));
        return true;
    };

    out.marks.clear();
    size_t o = byteOffset;
    uint32_t index = 0;
    uint32_t cumIL = 0;

    while (o + hdrBytes <= sectionEnd) {
        uint32_t word = u24 ? readU24BE_Nsmp(&bytes[o]) : readU32BE(&bytes[o]);
        uint32_t sampleCnt = word & 0x3fff;
        int filterOrder    = (word >> 14) & 0xf;
        bool loopDecay     = ((word >> 18) & 1) != 0;
        int bitWidth       = ((word >> 19) & 0xf) + 1;
        bool linMode       = ((word >> 23) & 1) != 0;
        bool isStop        = (filterOrder == 0 && bitWidth == 1);

        if (linMode) {
            out.marks.push_back({
                static_cast<uint32_t>((o - byteOffset) / hdrBytes),
                wordInterleaved ? cumIL : index,
                loopDecay
            });
        }
        o += hdrBytes;

        if (wordInterleaved) {
            if (sampleCnt == 0) continue;
            if (filterOrder > 7) break;
            cumIL += sampleCnt;
            uint32_t perCh = sampleCnt / channelCount;
            uint32_t wordsPerCh = (perCh * bitWidth + 31) / 32;
            if (o + static_cast<size_t>(wordsPerCh) * channelCount * 4 > sectionEnd) break;

            uint64_t mask = (bitWidth == 32) ? 0xFFFFFFFFULL : ((1ULL << bitWidth) - 1ULL);
            for (int ch = 0; ch < channelCount; ++ch) {
                uint32_t count = (ch == channelCount - 1) ? (sampleCnt - perCh * (channelCount - 1)) : perCh;
                uint64_t acc = 0;
                int nbits = 0;
                uint32_t produced = 0;
                for (uint32_t w = 0; w < wordsPerCh; ++w) {
                    uint32_t wVal = readU32BE(&bytes[o + (w * channelCount + ch) * 4]);
                    acc |= (static_cast<uint64_t>(wVal) << (32 - nbits));
                    nbits += 32;
                    while (nbits >= bitWidth && produced < count) {
                        uint32_t rawBits = static_cast<uint32_t>((acc >> (64 - bitWidth)) & mask);
                        if (!emitSample(ch, signExtendNw1(rawBits, bitWidth), filterOrder)) return false;
                        produced++;
                        acc <<= bitWidth;
                        nbits -= bitWidth;
                    }
                }
            }
            o += static_cast<size_t>(wordsPerCh) * channelCount * 4;
            continue;
        }

        // Codec 3 y Legacy (.nsmp): flujo entrelazado por muestras terminado por isStop
        if (isStop) break;
        if (filterOrder > 7 || sampleCnt == 0) return false;

        uint32_t nWords = (sampleCnt * bitWidth + wordBits - 1) / wordBits;
        if (o + static_cast<size_t>(nWords) * wordBytes > sectionEnd) return false;

        uint64_t mask = (bitWidth == 32) ? 0xFFFFFFFFULL : ((1ULL << bitWidth) - 1ULL);
        uint64_t acc = 0;
        int nbits = 0;
        uint32_t produced = 0;
        for (uint32_t w = 0; w < nWords; ++w) {
            uint32_t wVal = u24 ? (readU24BE_Nsmp(&bytes[o]) << 8) : readU32BE(&bytes[o]);
            o += wordBytes;
            acc |= (static_cast<uint64_t>(wVal) << (32 - nbits));
            nbits += wordBits;
            while (nbits >= bitWidth && produced < sampleCnt) {
                uint32_t rawBits = static_cast<uint32_t>((acc >> (64 - bitWidth)) & mask);
                if (!emitSample(index % channelCount, signExtendNw1(rawBits, bitWidth), filterOrder)) return false;
                produced++;
                index++;
                acc <<= bitWidth;
                nbits -= bitWidth;
            }
        }
    }

    if (chs[0].samples.size() < 100) return false;
    out.channelCount = channelCount;
    out.endOffset = o;
    out.chL = std::move(chs[0].samples);
    out.chR = (channelCount > 1) ? std::move(chs[1].samples) : out.chL;
    return true;
}

} // namespace

// En src/loaders/BankLoader.cpp reemplaza las llamadas .nsmp por:
bool BankLoader::loadNsmpNative(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg) {
    return NsmpLoader::loadNsmpNative(filePath, outInstrument, outStatusMsg);
}

bool BankLoader::loadNsmpFallback(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg) {
    return NsmpLoader::loadNsmpFallback(filePath, outInstrument, outStatusMsg);
}

bool BankLoader::loadClaviaProgramPreset(const std::string& filePath, PianoInstrument& ioInstrument, std::string& outStatusMsg) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> bin((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bin.size() < 32) return false;

    ClaviaHeaderInfo hdr = inspectClaviaHeader(filePath);
    SynthPresetParams& p = ioInstrument.defaultPreset;

    uint8_t bCut = bin.size() > 42 ? bin[42] : 220;
    uint8_t bRes = bin.size() > 45 ? bin[45] : 25;
    uint8_t bAtk = bin.size() > 50 ? bin[50] : 2;
    uint8_t bRel = bin.size() > 54 ? bin[54] : 50;

    p.cutoffHz = 600.0f * std::pow(33.0f, bCut / 255.0f);
    p.resonanceQ = 0.707f + (bRes / 255.0f) * 3.5f;
    p.attackSec = 0.001f + std::pow(bAtk / 255.0f, 2.5f) * 1.2f;
    p.releaseSec = 0.10f + (bRel / 255.0f) * 1.8f;

    outStatusMsg = "Preset de piano aplicado: " + hdr.displayName + " sobre el instrumento actual.";
    return true;
}

bool BankLoader::loadWaveFile(const std::string& wavPath, StrokeSample& s) {
    drwav wav;
    if (!drwav_init_file(&wav, wavPath.c_str(), nullptr)) return false;

    uint64_t totalFrames = wav.totalPCMFrameCount;
    uint32_t ch = wav.channels;
    s.sampleRate = wav.sampleRate ? wav.sampleRate : 44100;
    if (totalFrames == 0 || ch == 0) { drwav_uninit(&wav); return false; }

    // Reject malformed/oversized WAVs before allocation; 512 MiB per source limit.
    if(totalFrames>uint64_t(512)*1024*1024/(sizeof(float)*ch) || totalFrames>UINT32_MAX){drwav_uninit(&wav);return false;}
    std::vector<float> pSampleData(totalFrames * ch);
    uint64_t framesRead = drwav_read_pcm_frames_f32(&wav, totalFrames, pSampleData.data());
    drwav_uninit(&wav);
    if (framesRead < 4) return false;

    s.frames = static_cast<uint32_t>(framesRead);
    s.pcmL.resize(framesRead);
    s.pcmR.resize(framesRead);

    if (ch == 1) {
        for(size_t i=0;i<framesRead;++i){
            const auto q=static_cast<int16_t>(std::lrint(std::clamp(pSampleData[i],-1.f,1.f)*32767.f));
            s.pcmL[i]=q;s.pcmR[i]=q;
        }
    } else {
        for (size_t i = 0; i < framesRead; ++i) {
            s.pcmL[i] = static_cast<int16_t>(std::lrint(std::clamp(pSampleData[i*ch],-1.f,1.f)*32767.f));
            s.pcmR[i] = static_cast<int16_t>(std::lrint(std::clamp(pSampleData[i*ch+1],-1.f,1.f)*32767.f));
        }
    }
    // This WAV helper currently serves SFZ regions. Loop metadata is applied
    // by the SFZ importer, so do not modify an explicit one-shot or factory
    // loop by inventing a piano-style splice here.
    s.sampleScale=1.f/32768.f;
    s.readyFrames->store(s.frames,std::memory_order_release);
    return true;
}

void BankLoader::loadWavesInParallel(std::vector<StrokeSample>& strokes, const std::vector<std::string>& wavPaths) {
    const size_t total = strokes.size();
    if (total == 0) return;

    unsigned int numThreads = std::clamp(std::thread::hardware_concurrency(), 1u, 4u);
    numThreads = std::min<unsigned int>(numThreads, static_cast<unsigned int>(total));

    std::atomic<size_t> nextIndex{0};
    std::vector<std::thread> workers;
    for (unsigned int t = 0; t < numThreads; ++t) {
        workers.emplace_back([&]() {
            while (true) {
                size_t i = nextIndex.fetch_add(1, std::memory_order_relaxed);
                if (i >= total) break;
                if (!loadWaveFile(wavPaths[i], strokes[i])) strokes[i].frames = 0;
            }
        });
    }
    for (auto& th : workers) if (th.joinable()) th.join();

    strokes.erase(
        std::remove_if(strokes.begin(), strokes.end(), [](const StrokeSample& s) { return s.frames < 4; }),
        strokes.end()
    );
}

// SFZ 1.x: <control>/<global>/<master>/<group>/<region> inheritance, multiline
// opcodes, quoted sample paths, MIDI key names, velocity zones and sample loops.
// Non-PCM compressed formats and script opcodes are intentionally not interpreted.
bool BankLoader::loadFromSfz(const std::string& sfzFilePath, PianoInstrument& outInstrument) {
    fs::path sfzPath(sfzFilePath);
    if (!fs::is_regular_file(sfzPath)) return false;
    std::ifstream input(sfzPath);
    if (!input) return false;
    std::string content((std::istreambuf_iterator<char>(input)), {});
    if (content.size() > 16 * 1024 * 1024) return false;
    // Strip // and /* */ comments without disturbing quoted paths.
    std::string clean; clean.reserve(content.size());
    bool quoted=false, lineComment=false, blockComment=false;
    for(size_t i=0;i<content.size();++i){
        char c=content[i],n=(i+1<content.size()?content[i+1]:'\0');
        if(lineComment){if(c=='\n'){lineComment=false;clean+='\n';}continue;}
        if(blockComment){if(c=='*'&&n=='/'){blockComment=false;++i;}else if(c=='\n')clean+='\n';continue;}
        if(c=='"') quoted=!quoted;
        if(!quoted&&c=='/'&&n=='/'){lineComment=true;++i;continue;}
        if(!quoted&&c=='/'&&n=='*'){blockComment=true;++i;continue;}
        clean+=c;
    }
    using Opcodes=std::map<std::string,std::string>;
    auto trim=[](std::string v){
        const auto first=v.find_first_not_of(" \t\r\n\"");
        if(first==std::string::npos)return std::string{};
        auto last=v.find_last_not_of(" \t\r\n\"");
        return v.substr(first,last-first+1);
    };
    auto midiKey=[&](const std::string& t,int fallback){
        if(t.empty())return fallback;
        try{size_t consumed=0;int v=std::stoi(t,&consumed);if(consumed==t.size())return std::clamp(v,0,127);}catch(...){}
        char c=char(std::toupper(static_cast<unsigned char>(t[0])));
        auto base=std::string("CDEFGAB").find(c);
        if(base==std::string::npos)return fallback;
        const int offsets[]={0,2,4,5,7,9,11};
        int semitone=offsets[base];size_t i=1;
        if(i<t.size()&&(t[i]=='#'||t[i]=='b')){semitone+=(t[i]=='#'?1:-1);++i;}
        try{return std::clamp((std::stoi(t.substr(i))+1)*12+semitone,0,127);}catch(...){return fallback;}
    };
    auto number=[&](const Opcodes& ops,const char* name,double fallback){
        auto it=ops.find(name);if(it==ops.end())return fallback;
        try{double v=std::stod(it->second);return std::isfinite(v)?v:fallback;}catch(...){return fallback;}
    };
    Opcodes control,global,master,group,region;
    std::vector<StrokeSample> strokes;
    std::vector<std::string> paths;
    std::vector<int64_t> loopStart,loopEnd,offsets;
    std::vector<bool> regionLoop;
    auto combine=[&](){Opcodes merged=control;for(const auto& level:{global,master,group,region})
        for(const auto& kv:level)merged[kv.first]=kv.second;return merged;};
    auto submit=[&](){
        if(region.empty())return;
        Opcodes o=combine();auto it=o.find("sample");if(it==o.end())return;
        std::string sample=trim(it->second);if(sample.empty())return;
        for(char& c:sample)if(c=='\\')c='/';
        fs::path folder=sfzPath.parent_path();
        auto prefix=o.find("default_path");
        if(prefix!=o.end()){
            std::string d=trim(prefix->second);for(char& c:d)if(c=='\\')c='/';
            folder/=d;
        }
        fs::path wav=folder/sample;
        if(!fs::is_regular_file(wav))wav=sfzPath.parent_path()/sample;
        std::string ext=wav.extension().string();
        std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(ext!=".wav"&&!ext.empty())return; // PCM WAV; avoid pretending FLAC/OGG work
        StrokeSample s;
        s.index=static_cast<int>(strokes.size());
        const int key=midiKey(o.count("key")?o.at("key"):"",60);
        s.lokey=midiKey(o.count("lokey")?o.at("lokey"):"",o.count("key")?key:0);
        s.hikey=midiKey(o.count("hikey")?o.at("hikey"):"",o.count("key")?key:127);
        if(s.lokey>s.hikey)std::swap(s.lokey,s.hikey);
        s.actualRootMidi=midiKey(o.count("pitch_keycenter")?o.at("pitch_keycenter"):"",key);
        s.actualRootMidi=std::clamp(s.actualRootMidi-static_cast<int>(number(o,"transpose",0)),0,127);
        s.fineTuneCents=static_cast<float>(std::clamp(number(o,"tune",0),-1200.,1200.));
        s.lovel=std::clamp(int(number(o,"lovel",1)),1,127);
        s.hivel=std::clamp(int(number(o,"hivel",127)),1,127);
        if(s.lovel>s.hivel)std::swap(s.lovel,s.hivel);
        const double volume=std::clamp(number(o,"volume",0),-96.,24.);
        s.levelGainLinear=static_cast<float>(std::pow(10.0,volume/20.0));
        bool looping=o.count("loop_mode")&&(o.at("loop_mode")=="loop_continuous"||o.at("loop_mode")=="loop_sustain");
        const int64_t ls=static_cast<int64_t>(number(o,"loop_start",-1));
        const int64_t le=static_cast<int64_t>(number(o,"loop_end",-1));
        if(ls>=0 && le>ls)looping=true;
        if(o.count("loop_mode")&&(o.at("loop_mode")=="no_loop"||o.at("loop_mode")=="one_shot"))looping=false;
        s.loop.hasLoop=looping;
        strokes.push_back(std::move(s));paths.push_back(wav.string());
        loopStart.push_back(ls);loopEnd.push_back(le);
        offsets.push_back(std::max<int64_t>(0,int64_t(number(o,"offset",0))));
        regionLoop.push_back(looping);
    };
    std::string scope;
    size_t pos=0;
    while(pos<clean.size()){
        while(pos<clean.size()&&std::isspace(static_cast<unsigned char>(clean[pos])))++pos;
        if(pos>=clean.size())break;
        if(clean[pos]=='<'){
            const auto end=clean.find('>',pos+1);if(end==std::string::npos)break;
            std::string tag=trim(clean.substr(pos+1,end-pos-1));
            std::transform(tag.begin(),tag.end(),tag.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(tag=="region") {submit();region.clear();}
            else if(tag=="group"){submit();region.clear();group.clear();}
            else if(tag=="master"){submit();region.clear();master.clear();group.clear();}
            else if(tag=="global"){submit();region.clear();global.clear();master.clear();group.clear();}
            scope=tag;pos=end+1;continue;
        }
        const size_t begin=pos;
        while(pos<clean.size()&&(std::isalnum(static_cast<unsigned char>(clean[pos]))||clean[pos]=='_'))++pos;
        if(begin==pos){++pos;continue;}
        std::string key=clean.substr(begin,pos-begin);
        while(pos<clean.size()&&(clean[pos]==' '||clean[pos]=='\t'))++pos;
        if(pos>=clean.size()||clean[pos]!='=')continue;
        ++pos;while(pos<clean.size()&&(clean[pos]==' '||clean[pos]=='\t'))++pos;
        const size_t start=pos;
        if(pos<clean.size()&&clean[pos]=='"'){
            ++pos;while(pos<clean.size()&&clean[pos]!='"')++pos;
            if(pos<clean.size())++pos;
        }else{
            // Values can include spaces (e.g. sample=Grand Piano C4.wav). Stop at next opcode.
            while(pos<clean.size()&&clean[pos]!='<'){
                if(clean[pos]=='\n'){
                    // Allow multiline SFZ parameters; terminate only when next token is key=.
                }
                if(std::isspace(static_cast<unsigned char>(clean[pos]))){
                    size_t next=pos;
                    while(next<clean.size()&&std::isspace(static_cast<unsigned char>(clean[next])))++next;
                    size_t k=next;
                    while(k<clean.size()&&(std::isalnum(static_cast<unsigned char>(clean[k]))||clean[k]=='_'))++k;
                    if(k>next&&k<clean.size()&&clean[k]=='=')break;
                }
                ++pos;
            }
        }
        auto value=trim(clean.substr(start,pos-start));
        Opcodes* dest=scope=="control"?&control:scope=="global"?&global:scope=="master"?&master:scope=="group"?&group:scope=="region"?&region:nullptr;
        if(dest&&!key.empty())(*dest)[key]=value;
    }
    submit();
    if(strokes.empty())return false;
    loadWavesInParallel(strokes,paths); // below: preserve indices after filtering failed WAVs
    for(auto& s:strokes){
        const int i=s.index;
        if(i<0||size_t(i)>=loopStart.size())continue;
        s.startFrameOffset=uint32_t(std::min<int64_t>(offsets[i],std::max<int64_t>(0,s.frames-1)));
        // SFZ defaults to one-shot; never invent an acoustic-piano sustain loop.
        if(!regionLoop[i]){s.loop.hasLoop=false;continue;}
        if(s.loop.hasLoop && loopStart[i]>=0 && loopEnd[i]>loopStart[i]){
            const auto start=uint32_t(std::min<int64_t>(loopStart[i],s.frames-1));
            const auto end=uint32_t(std::min<int64_t>(loopEnd[i],s.frames-1));
            if(end>start+16){s.loop.start=start;s.loop.end=end;s.loop.length=end-start;}
            else s.loop.hasLoop=false;
        }
    }
    outInstrument.id=g_nextInstrumentId.fetch_add(1);
    outInstrument.strokes=std::move(strokes);
    outInstrument.name=sfzPath.stem().string();
    outInstrument.formatType="SFZ (PCM WAV)";
    outInstrument.sourcePath=sfzPath.string();
    outInstrument.zoneCount=static_cast<int>(outInstrument.strokes.size());
    size_t bytes=0;for(const auto& st:outInstrument.strokes)bytes+=(st.pcmL.size()+st.pcmR.size())*sizeof(int16_t);
    outInstrument.memoryMB=double(bytes)/(1024.0*1024.0);
    return !outInstrument.strokes.empty();
}

bool BankLoader::loadFromDirectory(const std::string& folderPath, PianoInstrument& outInstrument) {
    fs::path baseDir(folderPath);
    if (!fs::exists(baseDir)) return false;

    for (const auto& entry : fs::directory_iterator(baseDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if(ext == ".sfz" && loadFromSfz(entry.path().string(),outInstrument))return true;
        if(ext == ".sf2"){std::string err;if(Sf2Loader::load(entry.path().string(),outInstrument,err))return true;}
        if (ext == ".npno") {
            std::string msg;
            if (loadNpnoNative(entry.path().string(), outInstrument, msg)) return true;
        }
    }
    return false;
}

bool BankLoader::loadAny(const std::string& pathStr, PianoInstrument& outInstrument, std::string& outStatusMsg) {
    fs::path p(pathStr);
    if (!fs::exists(p)) {
        outStatusMsg = "La ruta no existe: " + pathStr;
        return false;
    }

    if (fs::is_directory(p)) {
        if (loadFromDirectory(p.string(), outInstrument)) {
            outStatusMsg = "Piano activo: " + outInstrument.name +
                           " (" + std::to_string(outInstrument.strokes.size()) + " muestras PCM)";
            return true;
        }
        outStatusMsg = "Selecciona un .npno, .nsmp, .sfz o .sf2 de la lista.";
        return false;
    }

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".npno" || ext == ".nsp") {
        return loadNpnoNative(p.string(), outInstrument, outStatusMsg);
    }

    if (ext == ".nsmp" || ext == ".nsmp3" || ext == ".nsmp4") {
        if (loadNsmpNative(p.string(), outInstrument, outStatusMsg)) return true;
        if (loadNpnoNative(p.string(), outInstrument, outStatusMsg)) return true;
        return loadNsmpFallback(p.string(), outInstrument, outStatusMsg);
    }

    if (ext == ".ns3f" || ext == ".ns2p" || ext == ".ns2f" || ext == ".nw2p") {
        return loadClaviaProgramPreset(p.string(), outInstrument, outStatusMsg);
    }

    if (ext == ".sf2") return Sf2Loader::load(p.string(),outInstrument,outStatusMsg);
    if (ext == ".sfz") {
        if (loadFromSfz(p.string(), outInstrument)) {
            outStatusMsg = "SFZ cargado: " + outInstrument.name;
            return true;
        }
        outStatusMsg = "SFZ sin muestras WAV válidas. Comprueba default_path, rutas, mayúsculas y formato PCM/float WAV.";
        return false;
    }

    outStatusMsg = "Formato no soportado: " + ext;
    return false;
}

std::vector<LibraryEntry> BankLoader::scanLibraryDirectory(const std::string& rootDir) {
    std::vector<LibraryEntry> entries;
    fs::path root(rootDir);
    if (!fs::exists(root) || !fs::is_directory(root)) return entries;

    const std::vector<std::string> validExts = {
        ".npno", ".nsmp", ".nsmp3", ".nsmp4", ".nsp", ".ns3f", ".ns2p", ".ns2f", ".nw2p", ".sfz", ".sf2"
    };

    auto classifySound = [](const std::string& filename, const std::string& ext, const ClaviaHeaderInfo& hdr) -> std::string {
        std::string low = filename + " " + hdr.displayName + " " + hdr.variant;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);

        if (ext == ".ns3f" || ext == ".ns2p" || ext == ".ns2f" || ext == ".nw2p") return "Programs";
        if (low.find("accord") != std::string::npos || low.find("acord") != std::string::npos ||
            low.find("harmoni") != std::string::npos || low.find("bandon") != std::string::npos ||
            low.find("melodica") != std::string::npos || low.find("organ") != std::string::npos ||
            low.find("farfisa") != std::string::npos || low.find("vox") != std::string::npos ||
            low.find("b3") != std::string::npos || low.find("pipe") != std::string::npos) {
            return "Accordion & Organ";
        }
        if (low.find("xylo") != std::string::npos || low.find("xilof") != std::string::npos ||
            low.find("marimba") != std::string::npos || low.find("vibra") != std::string::npos ||
            low.find("vibes") != std::string::npos || low.find("mallet") != std::string::npos ||
            low.find("kalimba") != std::string::npos || low.find("glocken") != std::string::npos ||
            low.find("celesta") != std::string::npos || low.find("bell") != std::string::npos ||
            low.find("music box") != std::string::npos || (ext == ".npno" && hdr.pianoTypeByte == 16)) {
            return "Xylophone & Mallets";
        }
        if (low.find("rhodes") != std::string::npos || low.find("wurl") != std::string::npos ||
            low.find("clav") != std::string::npos || low.find("ep1") != std::string::npos ||
            low.find("ep2") != std::string::npos || low.find("ep3") != std::string::npos ||
            low.find("ep4") != std::string::npos || low.find("ep5") != std::string::npos ||
            low.find("ep6") != std::string::npos || low.find("ep7") != std::string::npos ||
            low.find("ep8") != std::string::npos || low.find("ep9") != std::string::npos ||
            low.find("dx7") != std::string::npos || low.find("digigrand") != std::string::npos ||
            low.find("cp80") != std::string::npos || low.find("electric") != std::string::npos ||
            (ext == ".npno" && (hdr.pianoTypeByte == 1 || hdr.pianoTypeByte == 2 || hdr.pianoTypeByte == 3 ||
                                hdr.pianoTypeByte == 4 || hdr.pianoTypeByte == 14 || hdr.pianoTypeByte == 15))) {
            return "Electric Pianos";
        }
        if (low.find("string") != std::string::npos || low.find("violin") != std::string::npos ||
            low.find("cello") != std::string::npos || low.find("orchestra") != std::string::npos ||
            low.find("choir") != std::string::npos || low.find("voice") != std::string::npos ||
            low.find("brass") != std::string::npos || low.find("trumpet") != std::string::npos ||
            low.find("flute") != std::string::npos || low.find("sax") != std::string::npos ||
            low.find("pad") != std::string::npos || low.find("mellotron") != std::string::npos) {
            return "Strings, Brass & Pads";
        }
        if (ext == ".npno" || low.find("grand") != std::string::npos ||
            low.find("upright") != std::string::npos || low.find("piano") != std::string::npos ||
            low.find("steinway") != std::string::npos || low.find("bosendorfer") != std::string::npos ||
            low.find("yamaha") != std::string::npos || low.find("bambino") != std::string::npos ||
            low.find("saloon") != std::string::npos || low.find("felt") != std::string::npos) {
            return "Acoustic Pianos";
        }
        return "Synth & Misc Samples";
    };

    try {
        for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (std::find(validExts.begin(), validExts.end(), ext) != validExts.end()) {
                ClaviaHeaderInfo hdr = inspectClaviaHeader(entry.path().string());
                std::string cat = "SAMPLE";
                if (ext == ".npno") cat = "PIANO (.npno)";
                else if (ext == ".ns3f" || ext == ".ns2p" || ext == ".ns2f" || ext == ".nw2p") cat = "PROGRAM (" + ext + ")";
                else if (ext == ".sfz") cat = "SFZ";
                else if (ext == ".sf2") cat = "SOUNDFONT SF2";
                else cat = "SAMPLE (" + ext + ")";

                std::string group = classifySound(entry.path().filename().string(), ext, hdr);
                std::error_code ec;
                uintmax_t sz = fs::file_size(entry.path(), ec);

                entries.push_back({
                    entry.path().filename().string(), // Nombre original intacto
                    entry.path().generic_string(),    // Ruta con barras '/' uniformes
                    ext,
                    cat,
                    group,
                    sz,
                    true
                });
            }
        }
        std::sort(entries.begin(), entries.end(), [](const LibraryEntry& a, const LibraryEntry& b) {
            if ((a.extension == ".npno") != (b.extension == ".npno")) return a.extension == ".npno";
            return a.name < b.name;
        });
    } catch (...) {}

    return entries;
}

} // namespace SonusDSP