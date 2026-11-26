#pragma once
// Live Stage Studio Rack: four independently routable 5-slot effects chains.
// Slot DSP is constructed on the control thread, never inside the audio callback.
#include "StageDspEffects.h"
#include "StageTempo.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <memory>

namespace SonusDSP {
// An instance belongs to exactly one slot; no delay lines or modulation states
// are shared between slots, including when the same effect appears twice.
class StudioSlotDSP {
public:
    void init(float sr, int type) {
        sr_ = std::max(8000.f,sr);
        type_ = type;
        if(type==1 || type==2 || type==3) mod_.init(sr_);
        if(type==4) flanger_.init(sr_);
        if(type==5) delay_.init(sr_);
        if(type==6) { reverb_.init(sr_); atmospheric_.init(sr_); }   // v35: classic shimmer retired
        if(type==7 || type==8) color_.init(sr_);
        if(type==12){ slotEqCached_=false; }
        env_=0.f; phase_=0.f; holdL_=holdR_=0.f; holdCountdown_=0;
        compGain_=1.f;compDetLpL_=compDetLpR_=0.f;driveToneL_=driveToneR_=0.f;
        drivePrevL_=drivePrevR_=driveDcL_=driveDcR_=0.f;
    }
    int type() const {return type_;}
    // Called ONCE PER BLOCK on the audio thread, never allocates or locks.
    // DSP coefficients do not get recalculated at the audio sample rate.
    void syncParameters(const StudioSlotConfig& cfg){
        if(type_==12){
            if(slotEqCached_ && lastSlotEq_==cfg.p)return;
            slotEqCached_=true;lastSlotEq_=cfg.p;
            EqBandParams bands[5]{};
            const float boundsLo[3]={50.f,250.f,1000.f};
            const float boundsHi[3]={500.f,4000.f,16000.f};
            for(int i=0;i<3;++i){
                bands[i].freqHz=boundsLo[i]*std::pow(boundsHi[i]/boundsLo[i],std::clamp(cfg.p[6+i],0.f,1.f));
                bands[i].q=.4f*std::pow(20.f,std::clamp(cfg.p[i==0?9:(i==1?13:14)],0.f,1.f));
                bands[i].gainDb=(std::clamp(cfg.p[3+i],0.f,1.f)-.5f)*30.f;
            }
            for(int i=3;i<5;++i){bands[i].freqHz=1000.f;bands[i].q=1.f;bands[i].gainDb=0.f;}
            const float hp=20.f*std::pow(50.f,std::clamp(cfg.p[1],0.f,1.f));
            const float lp=400.f*std::pow(50.f,std::clamp(cfg.p[2],0.f,1.f));
            const int hpSlope=12*(1<<std::clamp(int(std::floor(cfg.p[10]*3.f)),0,2));
            const int lpSlope=12*(1<<std::clamp(int(std::floor(cfg.p[11]*3.f)),0,2));
            slotEq_.update(sr_,true,hp,lp,bands,hpSlope,lpSlope);
            slotEqOutputGain_=std::pow(10.f,(cfg.p[12]-.5f)*24.f/20.f);
        }
        if(type_==7 && cfg.version>=2){
            const float toneHz=700.f*std::pow(25.f,std::clamp(cfg.p[2],0.f,1.f));
            driveToneAlpha_=1.f-std::exp(-2.f*PI_F*toneHz/sr_);
            driveOutput_=std::pow(10.f,(cfg.p[4]-.5f)*24.f/20.f);
        }
        if(type_==9 && cfg.version>=2){
            // Smooth detector and gain, soft-knee, optional filtered sidechain.
            compAttack_=std::exp(-1.f/(sr_*(.0015f+.095f*cfg.p[3])));
            compRelease_=std::exp(-1.f/(sr_*(.035f+.75f*cfg.p[4])));
            compHpCoeff_=1.f-std::exp(-2.f*PI_F*(20.f+380.f*cfg.p[6])/sr_);
            compThreshold_=-42.f+40.f*cfg.p[0];
            compRatio_=1.f+19.f*cfg.p[1];
            compKnee_=18.f*cfg.p[5];
            compMakeup_=cfg.p[2]*18.f;
            compMix_=std::clamp(cfg.p[7],0.f,1.f);
        }
    }
    // Wet-only, so each source is present once in the dry mix.
    void process(float inL,float inR,const StudioSlotConfig& cfg,float& wetL,float& wetR,float masterBpm=120.f){
        wetL=wetR=0.f;
        const float p0=std::clamp(cfg.p[0],0.f,1.f),p1=std::clamp(cfg.p[1],0.f,1.f),
            p2=std::clamp(cfg.p[2],0.f,1.f),p3=std::clamp(cfg.p[3],0.f,1.f),
            p4=std::clamp(cfg.p[4],0.f,1.f),p5=std::clamp(cfg.p[5],0.f,1.f);
        float l=inL,r=inR;
        switch(type_){
            case 1: // Chorus
                mod_.processStereo(l,r,false,1,0,false,1,0,0,0,true,
                    .12f+5.88f*p0,p1,1.f,
                    cfg.version>=2?1+int(std::lround(cfg.p[6]*2.f)):2,
                    p4,cfg.version>=2?(p2*.8f-.4f):(.1f+p2*.3f),
                    2000.f+14000.f*p5,
                    cfg.version>=2?7.f+21.f*cfg.p[8]:7.f+p3*21.f);
                break;
            case 2: // Phaser
                mod_.processStereo(l,r,false,1,0,true,.1f+5.9f*p0,p1,p2*.8f,1.f,false,1,0,0,
                    2,.85f,.08f,9500.f,16.f,
                    cfg.version>=2?2+2*int(std::round(cfg.p[4]*3.f)):6,
                    cfg.version>=2?50.f+550.f*cfg.p[5]:220.f,
                    cfg.version>=2?1000.f+7000.f*cfg.p[6]:3620.f,
                    cfg.version>=2?cfg.p[7]:.25f);
                break;
            case 3: // Tremolo
                mod_.processStereo(l,r,true,.5f+13.5f*p0,p1,false,1,0,0,0,false,1,0,0);
                break;
            case 4: // Flanger
                flanger_.processStereo(l,r,.05f+4.95f*p0,p1,p2*1.6f-.8f,1.f);
                break;
            case 5: { // Stereo delay
                const bool sync=cfg.p[6]>=.5f;
                const int division=std::clamp(int(std::floor(cfg.p[7]*16.f)),0,15);
                // v3: NOTE always defines the subdivision. SYNC only selects BPM source.
                // Local BPM survives Performance / Program save and never changes masterBpm.
                const float localBpm=30.f+270.f*std::clamp(cfg.p[8],0.f,1.f);
                const float time=cfg.version>=3
                    ?tempoMilliseconds(sync?masterBpm:localBpm,division)
                    :(sync?tempoMilliseconds(masterBpm,division):40.f+1450.f*p3);
                delay_.processStereo(l,r,true,time,p2*.88f,p5,1.f,p4>=.5f);
                break;
            }
            case 6: { // Room, Plate, Hall, Shimmer (p5 selects model)
                int model=std::clamp(int(p5*3.999f),0,3);
                if(model==3) {
                    // Shimmer Atmosphere v5 (StageAtmosphericShimmer). v35: the v1–v4
                    // engines are retired; slots stored with them are cleared when a
                    // program is loaded (ParamSerialization), so every shimmer is v5.
                    // Reconfigured only when a parameter changes (no IIR design per sample).
                    if(!shimmerConfigured_ || lastShimmerVersion_ != cfg.version ||
                       !std::equal(cfg.p.begin(),cfg.p.end(),lastShimmer_.begin())) {
                        lastShimmer_=cfg.p;
                        lastShimmerVersion_=cfg.version;
                        shimmerConfigured_=true;
                        const float semitones=-24.f+48.f*std::clamp(cfg.p[6],0.f,1.f);
                        const float feedback=.992f*std::clamp(cfg.p[7],0.f,1.f);
                        const float lowcut=35.f+600.f*std::clamp(cfg.p[8],0.f,1.f);
                        const float highcut=1800.f+11500.f*std::clamp(cfg.p[9],0.f,1.f);
                        const float rate=.05f+2.95f*std::clamp(cfg.p[10],0.f,1.f);
                        const float depth=.65f*std::clamp(cfg.p[11],0.f,1.f);
                        const int pitchMode=std::clamp(int(cfg.p[12]*4.999f),0,4);
                        const int stereoMode=std::clamp(int(cfg.p[13]*3.999f),0,3);
                        const int color=std::clamp(int(cfg.p[14]*1.999f),0,1);
                        const float diffusion=.42f+.5f*std::clamp(cfg.p[15],0.f,1.f);
                        atmospheric_.setParams(1.f,semitones,feedback,diffusion,.10f+.88f*p4,
                            lowcut,highcut,rate,depth,
                            static_cast<ShimmerReverbEngine::PitchMode>(pitchMode),
                            static_cast<ShimmerReverbEngine::ReverbMode>(stereoMode),
                            static_cast<ShimmerReverbEngine::ColorMode>(color));
                        atmospheric_.setPreDelayMs(240.f*p3);
                        atmospheric_.setPitchReturnAmount(pitchMode==0 ? 0.f : p2);
                        atmospheric_.setDamping(p1);
                        atmospheric_.setMultiPitch(-24.f+48.f*cfg.p[16],cfg.p[17],
                            65.f+140.f*cfg.p[18],cfg.p[19],cfg.p[20],cfg.p[21]);
                        atmospheric_.setAtmosphere(cfg.p[22],cfg.p[23]>=.5f);
                    }
                    atmospheric_.processStereo(l,r);   // 100 % wet: no dry copy to remove
                    // Same normalized wet gain as other reverbs, no dry duplication.
                    wetL=std::clamp(l*p0*.82f,-3.f,3.f);
                    wetR=std::clamp(r*p0*.82f,-3.f,3.f);
                    return;
                }
                const float revMix=.78f;
                reverb_.processStereo(l,r,true,model,revMix,.05f+.95f*p4,p1,240.f*p3,0.f,
                    cfg.version>=2?cfg.p[6]:.5f,
                    cfg.version>=2?cfg.p[7]:.5f,
                    cfg.version>=2?cfg.p[8]:.5f);
                const float dryFactor=1.f-revMix*.28f;
                l-=inL*dryFactor; r-=inR*dryFactor;
                wetL=std::clamp(l*p0,-3.f,3.f); wetR=std::clamp(r*p0,-3.f,3.f);
                return;
            }
            case 7: { // Drive: v2 has selectable character, tone and DC blocker.
                if(cfg.version<2){color_.processStereo(l,r,true,p0,false,1,0);break;}
                const float g=1.f+15.f*p0, bias=(p5-.5f)*.25f;
                const int shape=std::clamp(int(std::floor(cfg.p[1]*2.999f)),0,2);
                const float alpha=driveToneAlpha_;
                const float output=driveOutput_;
                auto shapeFn=[&](float x){
                    if(shape==1) return std::clamp(x,-1.f,1.f); // hard clip
                    if(shape==2){float v=std::fmod(std::abs(x),4.f);return std::copysign(v>2.f?4.f-v:v,x)*.5f;} // fold
                    return std::tanh(x); // soft
                };
                const float vL=shapeFn((l+bias)*g)-shapeFn(bias*g);
                const float vR=shapeFn((r+bias)*g)-shapeFn(bias*g);
                driveToneL_+=alpha*(vL-driveToneL_);
                driveToneR_+=alpha*(vR-driveToneR_);
                // 1-pole DC highpass, protects live racks at strong asymmetry.
                const float highL=driveToneL_-drivePrevL_+.997f*driveDcL_;
                const float highR=driveToneR_-drivePrevR_+.997f*driveDcR_;
                drivePrevL_=driveToneL_;drivePrevR_=driveToneR_;
                driveDcL_=highL;driveDcR_=highR;
                l=highL*output;r=highR*output;
                break;
            }
            case 8: // Auto-pan
                color_.processStereo(l,r,false,0,true,.05f+11.95f*p0,p1);break;
            case 9: { // Stereo-linked feed-forward compressor
                if(cfg.version>=2){
                    const float originalL=inL,originalR=inR;
                    const float absL=std::abs(inL),absR=std::abs(inR);
                    float detected=std::max(absL,absR);
                    if(cfg.p[6]>0.001f){
                        compDetLpL_+=compHpCoeff_*(inL-compDetLpL_);
                        compDetLpR_+=compHpCoeff_*(inR-compDetLpR_);
                        detected=std::max(std::abs(inL-compDetLpL_),std::abs(inR-compDetLpR_));
                    }
                    const float coefficient=detected>env_?compAttack_:compRelease_;
                    env_=detected+(env_-detected)*coefficient;
                    const float db=20.f*std::log10(std::max(env_,1.e-9f));
                    const float x=db-compThreshold_,slope=1.f-1.f/compRatio_;
                    float reduction=0.f;
                    if(compKnee_>0.001f && x>-compKnee_*.5f && x<compKnee_*.5f){
                        const float y=x+compKnee_*.5f;
                        reduction=-slope*y*y/(2.f*compKnee_);
                    }else if(x>=compKnee_*.5f)reduction=-slope*x;
                    const float target=std::pow(10.f,(reduction+compMakeup_)/20.f);
                    // Extra 2ms smoothing protects transients during automation.
                    compGain_+=(target-compGain_)*(.0008f+2.f/sr_);
                    l=originalL*(1.f-compMix_)+(originalL*compGain_)*compMix_;
                    r=originalR*(1.f-compMix_)+(originalR*compGain_)*compMix_;
                    break;
                }
                const float absolute=std::max(std::abs(inL),std::abs(inR));
                const float attack=std::exp(-1.f/(sr_*(.002f+.08f*p3)));
                const float release=std::exp(-1.f/(sr_*(.05f+.65f*p4)));
                env_=absolute+(env_-absolute)*(absolute>env_?attack:release);
                const float thrDb=-36.f+34.f*p0;
                const float ratio=1.f+15.f*p1;
                const float levelDb=20.f*std::log10(std::max(env_,1.e-8f));
                const float gainDb=(levelDb>thrDb)?(thrDb+(levelDb-thrDb)/ratio-levelDb):0.f;
                const float gain=std::pow(10.f,gainDb/20.f)*(1.f+p2*1.2f);
                l*=gain;r*=gain;break;
            }
            case 10: { // Bitcrusher and sample-rate reduction
                if(holdCountdown_==0){
                    const float steps=std::pow(2.f,4.f+12.f*p0);
                    holdL_=std::round(inL*steps)/steps;
                    holdR_=std::round(inR*steps)/steps;
                    holdCountdown_=1+int(p1*31.f);
                }
                --holdCountdown_;l=holdL_;r=holdR_;break;
            }
            case 11: { // Mid-side stereo width
                const float mid=(inL+inR)*.5f,side=(inL-inR)*.5f;
                const float width=.05f+2.45f*p0;
                l=mid+side*width;r=mid-side*width;break;
            }
            case 12: { // Parametric EQ insert: delta-return, unity at zero controls.
                slotEq_.processStereo(l,r);
                wetL=std::clamp((l*slotEqOutputGain_-inL)*p0,-4.f,4.f);
                wetR=std::clamp((r*slotEqOutputGain_-inR)*p0,-4.f,4.f);
                return;
            }
            default: return;
        }
        const float wetMix=(type_==5?1.f:p3); // for delay, time is p3; wet gain uses p0
        const float output=(type_==5?p0:(type_==9?1.f:wetMix));
        wetL=std::clamp((l-inL)*output,-4.f,4.f);
        wetR=std::clamp((r-inR)*output,-4.f,4.f);
    }
private:
    int type_=0;
    float sr_=48000.f,env_=0.f,phase_=0.f,holdL_=0.f,holdR_=0.f;
    int holdCountdown_=0;
    ModulationRack mod_;
    StageFlangerEffect flanger_;
    StereoDelayEffect delay_;
    StageReverbEffect reverb_;
    StageAtmosphericShimmer atmospheric_;
    std::array<float,STUDIO_PARAM_COUNT> lastShimmer_{};
    bool shimmerConfigured_=false;
    int lastShimmerVersion_=-1;
    StageColorEffects color_;
    MasterEqualizer slotEq_{};
    float slotEqOutputGain_=1.f;
    bool slotEqCached_=false;
    std::array<float,STUDIO_PARAM_COUNT> lastSlotEq_{};
    float compAttack_=0.99f,compRelease_=0.999f,compHpCoeff_=0.f;
    float compDetLpL_=0.f,compDetLpR_=0.f,compThreshold_=-18.f,compRatio_=4.f;
    float compKnee_=0.f,compMakeup_=0.f,compMix_=1.f,compGain_=1.f;
    float driveToneL_=0.f,driveToneR_=0.f,drivePrevL_=0.f,drivePrevR_=0.f;
    float driveDcL_=0.f,driveDcR_=0.f,driveToneAlpha_=.4f,driveOutput_=1.f;
};

// Audio-thread-owned rack states; EQ coefficients are recomputed only on dirty.
struct StudioRackDSP {
    MasterEqualizer eq;
    std::array<int,STUDIO_SLOTS> tailRemaining{};
    void updateEq(float sr,const StudioRackConfig& cfg){
        EqBandParams b[5];
        for(int i=0;i<5;i++){
            b[i].freqHz=std::clamp(cfg.eqFreq[i],30.f,std::min(16000.f,sr*.45f));
            b[i].q=std::clamp(cfg.eqQ[i],.3f,12.f);
            b[i].gainDb=std::clamp(cfg.eqDb[i],-15.f,15.f);
        }
        // Always active: render() crossfades ON/OFF (cfg.eqEnabled) without a click.
        eq.update(sr,true,cfg.eqHighPass,cfg.eqLowPass,b,cfg.eqHighPassSlope,cfg.eqLowPassSlope);
    }
};
}
