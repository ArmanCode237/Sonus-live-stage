#pragma once
// Per-layer insert chain (synth layers and piano layers): Juno-inspired Chorus I / II,
// Phaser I / II, a stereo echo and (v31) a Room / Plate / Hall reverb.
// Every buffer is allocated in init(); process() never allocates. Units that are
// switched off are skipped, and callers bypass the whole chain when no unit is on
// and nothing is fading (busy()).
// v32 live behaviour: a unit switched ON / OFF crossfades its wet path in 12 ms and
// MIX is smoothed (no clicks). A unit that has faded out is wiped before it is used
// again, so stale echoes never come back; maintain() spreads that wipe over several
// blocks instead of one multi-megabyte memset in the audio callback.
#include "NordDspEffects.h"
#include "SynthLayerInsertFx.h"

namespace NordDSP {
class SynthLayerInsertDSP {
    struct ModDelay {
        std::array<std::vector<float>, 2> line;
        size_t cursor = 0;
        float phase = 0.f;
        std::array<float,2> feedback{{0.f,0.f}};
        std::array<float,2> pre{{0.f,0.f}}, post{{0.f,0.f}};   // Juno BBD input / output filters
    };
    struct Phaser {
        std::array<std::array<float,6>,2> z{};
        std::array<float,2> feedback{{0.f,0.f}};
        float phase = 0.f;
    };
    std::array<ModDelay, 2> chorus_{};
    std::array<Phaser, 2> phasers_{};
    ModDelay echo_{};
    NordReverbEffect reverb_{};   // Room / Plate / Hall only: no Shimmer engine is allocated
    float sr_ = 48000.f;
    float junoPreK_ = .6f, junoPostK_ = .75f;   // one-pole coefficients, 7237 Hz / 10644 Hz
    float gateStep_ = 1.f / 576.f;   // 12 ms at 48 kHz
    float mixK_ = .005f;             // 10 ms one-pole for MIX
    std::array<float, INSERT_FX_UNITS> gate_{};    // 0 = silent / bypassed, 1 = fully in
    std::array<float, INSERT_FX_UNITS> mix_{};
    std::array<bool, INSERT_FX_UNITS> dirty_{};    // holds audio from its last use
    size_t echoClearPos_ = 0;
    static constexpr size_t kClearPerBlock = 32768; // floats per maintain() call (~128 KB)
    void clearUnit(int unit) {
        if (unit < 2) {
            auto& c = chorus_[unit];
            c.cursor = 0; c.phase = 0.f; c.feedback.fill(0.f); c.pre.fill(0.f); c.post.fill(0.f);
            for (auto& ch : c.line) std::fill(ch.begin(), ch.end(), 0.f);
        } else if (unit < 4) {
            auto& ph = phasers_[unit - 2];
            ph.phase = 0.f; ph.feedback.fill(0.f); for (auto& ch : ph.z) ch.fill(0.f);
        } else if (unit == INSERT_ECHO) {
            for (auto& ch : echo_.line) std::fill(ch.begin() + std::min(echoClearPos_, ch.size()), ch.end(), 0.f);
            echo_.cursor = 0; echo_.feedback.fill(0.f); echoClearPos_ = 0;
        } else {
            reverb_.clear();
        }
        dirty_[unit] = false;
    }
    static constexpr float kPi = 3.14159265358979323846f;
    static float fracRead(const std::vector<float>& v, size_t cursor, float delay) {
        if (v.empty()) return 0.f;
        const float len = float(v.size());
        float p = float(cursor) - std::clamp(delay,1.f,len-2.f);
        if (p < 0.f) p += len;
        const size_t a = size_t(p);
        const size_t b = (a + 1) % v.size();
        const float f = p - float(a);
        return v[a] * (1.f-f) + v[b] * f;
    }
    static float phaseSin(float phase) { return std::sin(2.f*kPi*phase); }
public:
    void init(float sampleRate) {
        sr_ = std::clamp(sampleRate,8000.f,192000.f);
        for (auto& c:chorus_) {
            for(auto& channel:c.line) channel.assign(size_t(sr_*.065f)+8,0.f);
        }
        for(auto& channel:echo_.line) channel.assign(size_t(sr_*8.1f)+8,0.f);
        reverb_.init(sr_, false);
        gateStep_ = 1.f / (0.012f * sr_);
        junoPreK_ = 1.f - std::exp(-2.f * kPi * 7237.f / sr_);
        junoPostK_ = 1.f - std::exp(-2.f * kPi * 10644.f / sr_);
        mixK_ = 1.f - std::exp(-1.f / (0.010f * sr_));
        reset();
    }
    /** A unit is still audible (on, or fading out): the caller must keep calling process(). */
    bool busy() const {
        for (float g : gate_) if (g > 0.f) return true;
        return false;
    }
    /** Once per audio block, also while the chain is bypassed: wipes units that have
        faded out, a bounded amount per call (echo lines are several megabytes). */
    void maintain() {
        for (int unit = 0; unit < INSERT_FX_UNITS; ++unit) {
            if (!dirty_[unit] || gate_[unit] > 0.f) continue;
            if (unit != INSERT_ECHO) { clearUnit(unit); continue; }
            const size_t len = echo_.line[0].size();
            const size_t end = std::min(len, echoClearPos_ + kClearPerBlock / 2);
            for (auto& ch : echo_.line) std::fill(ch.begin() + echoClearPos_, ch.begin() + end, 0.f);
            echoClearPos_ = end;
            if (end >= len) { echo_.cursor = 0; echo_.feedback.fill(0.f); echoClearPos_ = 0; dirty_[unit] = false; }
        }
    }
    void reset() {
        for(auto& c:chorus_){
            c.cursor=0;c.phase=0.f;c.feedback.fill(0.f);c.pre.fill(0.f);c.post.fill(0.f);
            for(auto& ch:c.line) std::fill(ch.begin(),ch.end(),0.f);
        }
        for(auto& p:phasers_){p.phase=0.f;p.feedback.fill(0.f);for(auto& ch:p.z)ch.fill(0.f);}
        echo_.cursor=0;echo_.feedback.fill(0.f);
        for(auto& ch:echo_.line)std::fill(ch.begin(),ch.end(),0.f);
        reverb_.clear();
        gate_.fill(0.f); mix_.fill(0.f); dirty_.fill(false); echoClearPos_ = 0;
    }
    void process(float& left, float& right,
                 const InsertFxChain& fx, float masterBpm=120.f) {
        float channels[2] = {left, right};
        // Disabled units are skipped entirely once faded out: no DSP work.
        for(int unit=0;unit<INSERT_FX_UNITS;unit++){
            const auto& p=fx[unit];
            const bool on=p.enabled && p.mix > 0.0001f;
            float& gate=gate_[unit];
            if(!on && gate<=0.f)continue;
            if(gate<=0.f){
                // Switching on: start from a clean unit and the current MIX.
                if(dirty_[unit])clearUnit(unit);
                mix_[unit]=std::clamp(p.mix,0.f,1.f);
            }
            gate=on?std::min(1.f,gate+gateStep_):std::max(0.f,gate-gateStep_);
            if(on)mix_[unit]+=(std::clamp(p.mix,0.f,1.f)-mix_[unit])*mixK_;
            dirty_[unit]=true;
            const float dryL=channels[0],dryR=channels[1];
            const float mix=mix_[unit];
            if(unit==INSERT_REVERB){
                reverb_.processStereo(channels[0],channels[1],true,std::clamp(p.type,0,2),mix,
                    std::clamp(p.depth,0.05f,1.f),std::clamp(p.damping,0.f,1.f),std::clamp(p.preDelayMs,0.f,250.f),0.f);
                channels[0]=dryL+(channels[0]-dryL)*gate;
                channels[1]=dryR+(channels[1]-dryR)*gate;
                continue;
            }
            const float depth=std::clamp(p.depth,0.f,1.f);
            const float fb=std::clamp(p.feedback,-.79f,.79f);
            if(unit<2){
                // v32.2 Roland Juno-60 BBD chorus (MN3009 bucket brigade), from the
                // service-manual figures: triangle LFO, 1.54-5.15 ms delay swing, the
                // right channel reads the inverted LFO, 7.2 kHz anti-alias filter before
                // the BBD and 10.6 kHz reconstruction filter after it.
                //   I  : RATE (0.513 Hz on the Juno)      II : RATE (0.863 Hz on the Juno)
                //   I + II (both units on): the Juno's fast mono vibrato, 9.75 Hz, 3.22-3.56 ms.
                // DEPTH scales the delay swing (0.75 = the Juno's), FEEDBACK is an extra.
                const bool otherOn=fx[1-unit].enabled && fx[1-unit].mix>0.0001f;
                if(unit==1 && otherOn){
                    // I+II is rendered by unit I; unit II stays transparent meanwhile.
                    channels[0]=dryL;channels[1]=dryR;
                    continue;
                }
                auto& c=chorus_[unit];
                const bool modeBoth=unit==0 && otherOn;
                const float speed=modeBoth?9.75f:std::clamp(p.rateHz,.05f,7.f);
                const float minMs=modeBoth?3.22f:1.54f, maxMs=modeBoth?3.56f:5.15f;
                const float swing=std::clamp(depth/0.75f,0.f,1.6f);
                const float centreMs=(minMs+maxMs)*.5f, halfMs=(maxMs-minMs)*.5f*swing;
                c.phase+=speed/sr_; if(c.phase>=1.f)c.phase-=1.f;
                const float tri=4.f*std::abs(c.phase-.5f)-1.f;           // -1..1 triangle
                for(int side=0;side<2;side++){
                    const float lfo=(modeBoth||side==0)?tri:-tri;
                    const float d=(centreMs+halfMs*lfo)*sr_*.001f;
                    float wet=fracRead(c.line[side],c.cursor,d);
                    c.post[side]+=(wet-c.post[side])*junoPostK_;
                    wet=c.post[side];
                    c.pre[side]+=(std::clamp(channels[side]+wet*fb,-3.f,3.f)-c.pre[side])*junoPreK_;
                    c.line[side][c.cursor]=c.pre[side];
                    channels[side]=channels[side]*(1.f-mix)+wet*mix;
                }
                c.cursor=(c.cursor+1)%c.line[0].size();
                channels[0]=dryL+(channels[0]-dryL)*gate;
                channels[1]=dryR+(channels[1]-dryR)*gate;
            }else if(unit<4){
                auto& ph=phasers_[unit-2];
                const int stages=unit==2?4:6;
                ph.phase=std::fmod(ph.phase+std::clamp(p.rateHz,.05f,7.f)/sr_,1.f);
                for(int side=0;side<2;side++){
                    float v=std::clamp(channels[side]+ph.feedback[side]*fb,-3.f,3.f);
                    const float lfo=.5f+.5f*phaseSin(ph.phase+(side?.23f:0.f));
                    const float hz=std::clamp(260.f+depth*3200.f*lfo,80.f,sr_*.38f);
                    const float g=std::tan(kPi*hz/sr_);
                    const float a=(1.f-g)/(1.f+g);
                    for(int s=0;s<stages;s++){
                        const float freqRatio=1.f+.24f*s;
                        const float aa=std::clamp(a/freqRatio,-.93f,.93f);
                        const float out=-aa*v+ph.z[side][s];
                        ph.z[side][s]=v+aa*out;
                        v=out;
                    }
                    ph.feedback[side]=v;
                    channels[side]=channels[side]*(1.f-mix)+v*mix;
                }
                channels[0]=dryL+(channels[0]-dryL)*gate;
                channels[1]=dryR+(channels[1]-dryR)*gate;
            }else if(unit==INSERT_ECHO){
                auto& c=echo_;
                // A small stereo tape-style drift makes RATE and DEPTH useful
                // controls on the delay as well, without changing the preset's
                // nominal delay time. DEPTH=0 provides a fixed, clean echo.
                c.phase=std::fmod(c.phase+std::clamp(p.rateHz,.05f,7.f)/sr_,1.f);
                const float drift=2.6f*depth*phaseSin(c.phase);
                const float baseMs=p.syncEnabled?tempoMilliseconds(masterBpm,p.syncDivision):p.timeMs;
                const float delayL=std::clamp(baseMs+drift,35.f,8000.f)*sr_*.001f;
                const float delayR=std::clamp(baseMs*1.016f-drift,35.f,8000.f)*sr_*.001f;
                const float wetL=fracRead(c.line[0],c.cursor,delayL);
                const float wetR=fracRead(c.line[1],c.cursor,delayR);
                c.line[0][c.cursor]=std::clamp(channels[0]+wetR*fb,-3.f,3.f);
                c.line[1][c.cursor]=std::clamp(channels[1]+wetL*fb,-3.f,3.f);
                channels[0]=channels[0]*(1.f-mix)+wetL*mix;
                channels[1]=channels[1]*(1.f-mix)+wetR*mix;
                c.cursor=(c.cursor+1)%c.line[0].size();
                channels[0]=dryL+(channels[0]-dryL)*gate;
                channels[1]=dryR+(channels[1]-dryR)*gate;
            }
        }
        left=std::isfinite(channels[0])?channels[0]:0.f;
        right=std::isfinite(channels[1])?channels[1]:0.f;
    }
};
} // namespace NordDSP
