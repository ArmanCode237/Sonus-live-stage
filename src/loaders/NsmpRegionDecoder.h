#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace NordDSP {

// Word offsets are relative to the start of a compressed STK stream. Samples
// count interleaved channel samples; convert to per-channel frames only here.
struct NsmpStreamMarker {
    uint32_t word = 0;
    uint32_t sample = 0;
    bool loopDecay = false;
};
struct NsmpDecodedRegions {
    bool headerValid = false;
    bool oneShot = false;
    bool hasLoop = false;
    uint32_t loopStart = 0;
    uint32_t alternateStart = 0;
    float decayCoef = 1.f;
    float decayDbPerSecond = 0.f; // signed: loss is negative
};
inline uint32_t nsmpU32BE(const uint8_t* p) {
    return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | uint32_t(p[3]);
}
inline NsmpDecodedRegions decodeNsmpRegions(const uint8_t* stk, size_t size,
        uint32_t frames, int channels, const std::vector<NsmpStreamMarker>& marks) {
    NsmpDecodedRegions result;
    if (!stk || size < 0x31 || frames<4) return result;
    const uint32_t u1=nsmpU32BE(stk+0x12),u2=nsmpU32BE(stk+0x1b);
    const uint32_t u3=nsmpU32BE(stk+0x24),u4=nsmpU32BE(stk+0x2d);
    if(!(u1<=u2 && u2<=u3 && u3<=u4 && u4>u1 && u4-u1<=(1u<<27)))return result;
    result.headerValid=true;
    result.oneShot=(u3==u4);
    if(marks.empty())return result;
    const uint32_t endWord=u4-u1, loopWord=u3-u1, altWord=u2-u1;
    const int64_t anchored=int64_t(marks.back().word)-int64_t(endWord);
    auto lookup=[&](uint32_t word,int64_t shift)->int64_t{
        const int64_t at=int64_t(word)+shift;
        if(at<0 || at>UINT32_MAX)return -1;
        for(const auto& mark:marks)if(mark.word==uint32_t(at))
            return int64_t(mark.sample/uint32_t(std::max(1,channels)));
        return -1;
    };
    int64_t shift=anchored;
    if(!result.oneShot && lookup(loopWord,shift)<0)shift=0;
    const int64_t loopFrame=result.oneShot?int64_t(frames):lookup(loopWord,shift);
    if(!result.oneShot && (loopFrame<0 || loopFrame+4>int64_t(frames)))return result;
    const int64_t alt=lookup(altWord,shift);
    if(alt>=0 && alt<int64_t(frames))result.alternateStart=uint32_t(alt);
    if(result.oneShot)return result;
    result.hasLoop=true;result.loopStart=uint32_t(loopFrame);
    const uint32_t raw=(uint32_t(stk[0x28])<<16)|(uint32_t(stk[0x29])<<8)|stk[0x2a];
    const int32_t signedRaw=(raw&0x800000u)?int32_t(raw)-0x1000000:int32_t(raw);
    const float coef=std::abs(float(signedRaw))/8388608.f;
    if(coef>0.f && coef<=1.f){
        result.decayCoef=coef;
        result.decayDbPerSecond=20.f*std::log10(coef)*21700.f;
    }
    return result;
}
} // namespace NordDSP
