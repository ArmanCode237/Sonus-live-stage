// Sonus Live Stage — Shimmer Atmosphere v4 (engine revision 4.2).
// Included *inside* namespace SonusDSP in StageDspEffects.h, after StereoBiquad.
//
// Architecture (public API and the 24-parameter layout are unchanged):
//   input -> pre-delay -> [ early-reflection tap network, L/R cross-fed ]
//         -> 4(+2 BLOOM) all-pass diffusers -> 16-line Householder FDN
//   FDN   : RT60-derived decay per line, in-loop damping, low-frequency decay
//           shelf (no bass build-up), per-line modulation with independent
//           rates (complex oscillators, no per-sample sin/cos)
//   shimmer: dual-head Hann granular shifters (A + B) with 4-point Hermite
//           interpolation, re-injected into the tank (octave cascade)
//   output: late field with a coherent centre component (positive L/R
//           correlation, mono compatible) + early reflections + harmonics,
//           M/S width, equal-power dry/wet.
//
// Rev 4.1: wet level normalised from RT60, BLOOM / FREEZE / STEREO FIELD /
//          MOD DEPTH made functional, slewed controls, anti-denormal.
// Rev 4.2: analysis of the Hall reverb showed a coherent stereo image (L/R
//          correlation ~ +0.4) and early reflections; the shimmer was anti-phase
//          (-0.12) with no early field and 11 dB more sub-200 Hz energy. Fixed.
//
// All delay memory is allocated in init(); no allocation, locks or I/O in processStereo().
#pragma once
class StageAtmosphericShimmer {
public:
    using PitchMode = ShimmerReverbEngine::PitchMode;
    using ReverbMode = ShimmerReverbEngine::ReverbMode;
    using ColorMode = ShimmerReverbEngine::ColorMode;

    void init(float sr) {
        sr_ = std::clamp(sr,8000.f,192000.f);
        // Pairwise incommensurate delays make coloration less periodic.
        constexpr float lengthsMs[16]={62.3f,73.7f,89.3f,101.9f,119.7f,137.3f,159.1f,181.7f,
                                         66.7f,79.1f,93.7f,109.3f,127.9f,149.3f,167.9f,193.1f};
        for(int i=0;i<16;++i){
            baseDelay_[i]=lengthsMs[i]*sr_*.001f;
            lines_[i].assign(size_t(baseDelay_[i]*3.6f)+128,0.f);
            pos_[i]=0;lp_[i]=0.f;lfState_[i]=0.f;
            delay_[i]=delayTarget_[i]=baseDelay_[i];
            gains_[i]=gainsTarget_[i]=.9f;
            // Each line has its own modulation phase and rate (0.8x .. 1.25x).
            modC_[i]=std::cos(float(i)*.793f);
            modS_[i]=std::sin(float(i)*.793f);
        }
        // Pre-delay (<=240 ms) plus the longest early tap (<=~90 ms).
        preL_.assign(size_t(sr_*.34f)+16,0.f);
        preR_.assign(preL_.size(),0.f);
        static constexpr float offsets[kApStages]={3.23f,5.97f,9.17f,13.79f,19.31f,27.11f};
        for(int i=0;i<kApStages;++i){
            apL_[i].assign(size_t(offsets[i]*sr_*.001f)+4,0.f);
            apR_[i].assign(size_t((offsets[i]+.73f)*sr_*.001f)+4,0.f);
            apPosL_[i]=apPosR_[i]=0;
        }
        // Oversized grain ring supports pitch shifts and long crossfade windows
        // without ever indexing samples that were not previously written.
        grainL_.assign(size_t(sr_*.32f)+128,0.f);
        grainR_.assign(grainL_.size(),0.f);
        grainWrite_=0;preWrite_=0;
        grainPhaseA_[0]=.15;grainPhaseA_[1]=.29;
        grainPhaseB_[0]=.39;grainPhaseB_[1]=.53;
        for(size_t i=0;i<window_.size();++i)
            window_[i]=.5f-.5f*std::cos(2.f*PI_F*float(i)/float(window_.size()));
        grainSpan_=std::clamp(sr_*.125f,1024.f,float(grainL_.size())-128.f);
        grainSpanTarget_=grainSpan_;
        grainCoeff_=1.f-std::exp(-1.f/(sr_*.06f));
        ampCoeff_=1.f-std::exp(-1.f/(sr_*.02f));
        gainCoeff_=1.f-std::exp(-1.f/(sr_*.04f));
        freezeCoeff_=1.f-std::exp(-1.f/(sr_*.08f));
        lfCoeff_=1.f-std::exp(-2.f*PI_F*kLfCorner/sr_);
        ratioA_=targetRatioA_=2.;ratioB_=targetRatioB_=1.5;
        gainPitch_=targetGainPitch_=0.f;
        pitchToneL_=pitchToneR_=0.f;
        denormSign_=1.f;
        freezeAmt_=freezeTarget_=0.f;
        for(int k=0;k<kEarlyTaps;++k)erPos_[k][0]=erPos_[k][1]=erTarget_[k][0]=erTarget_[k][1]=1.f;
        wetLow_.reset();wetHigh_.reset();pitchLow_.reset();pitchHigh_.reset();
        setParams(1.f,12.f,.72f,.85f,.80f,140.f,11000.f,.23f,.15f,
                  PitchMode::Dual,ReverbMode::BigStereo,ColorMode::Bright);
        setMultiPitch(7.f,.30f,125.f,.5f,.8f,.7f);
        setPitchReturnAmount(.5f);
        setPreDelayMs(60.f);
        setDamping(.3f);
        setAtmosphere(.6f,false);
        snapToTargets();
    }
    void setParams(float mix,float pitch,float feedback,float diffusion,float size,
                   float lowCut,float highCut,float modRate,float modDepth,
                   PitchMode pitchMode,ReverbMode reverbMode,ColorMode color) {
        mix_=std::clamp(mix,0.f,1.f);
        targetRatioA_=std::pow(2.,double(std::clamp(pitch,-24.f,24.f))/12.);
        feedback_=std::clamp(feedback,0.f,.992f);
        diffusion_=std::clamp(diffusion,0.f,1.f);
        size_=std::clamp(size,.05f,1.f);
        modRate_=std::clamp(modRate,.05f,5.f);
        modDepth_=std::clamp(modDepth,0.f,1.f);
        pitchMode_=pitchMode;reverbMode_=reverbMode;color_=color;
        const float hf=std::clamp(highCut,600.f,sr_*.45f);
        const float lf=std::clamp(lowCut,20.f,1000.f);
        wetLow_.setHighPass(sr_,lf,.707f);
        wetHigh_.setLowPass(sr_,hf,.707f);
        pitchLow_.setHighPass(sr_,lf,.707f);
        pitchHigh_.setLowPass(sr_,hf,.707f);
        // STEREO FIELD: MONO sums the input, SMALL/MEDIUM/BIG change room scale,
        // early-reflection spacing and the stereo spread of the wet return.
        float modeScale=1.f;
        switch(reverbMode_){
            case ReverbMode::SmallStereo: modeScale=.72f; modeWidth_=.85f; break;
            case ReverbMode::MediumStereo: modeScale=1.f; modeWidth_=1.f; break;
            case ReverbMode::BigStereo: modeScale=1.2f; modeWidth_=1.12f; break;
            default: modeScale=1.f; modeWidth_=1.f; break;
        }
        updateWidth();
        sizeScale_=(.55f+size_*1.8f)*modeScale;
        // Modulation rates: a spread of ratios so lines never lock together.
        for(int i=0;i<16;++i){
            const float w=2.f*PI_F*modRate_*(.8f+.45f*float(i)/15.f)/sr_;
            modCr_[i]=std::cos(w);modSr_[i]=std::sin(w);
        }
        // Early-reflection geometry follows SIZE and STEREO FIELD.
        const float erScale=(.45f+size_*.85f)*(modeScale/1.0f)*.9f;
        static constexpr float tapsL[kEarlyTaps]={7.1f,11.3f,17.9f,23.7f,31.1f,41.3f,53.9f,67.7f};
        static constexpr float tapsR[kEarlyTaps]={8.9f,13.7f,19.3f,27.1f,35.9f,45.7f,57.1f,71.9f};
        for(int k=0;k<kEarlyTaps;++k){
            erTarget_[k][0]=std::max(8.f,tapsL[k]*erScale*sr_*.001f);
            erTarget_[k][1]=std::max(8.f,tapsR[k]*erScale*sr_*.001f);
        }
        // Feedback knob controls decay time in seconds. This is intentionally
        // longer than ordinary Hall, but a bounded RT60 rather than an unstable
        // coefficient arbitrarily approaching 1.0.
        const float f=feedback_/.992f;
        const float rt60=2.5f+110.f*f*f*f;
        float gSum=0.f;
        for(int i=0;i<16;++i){
            // Reserve plenty of margin for modulation and ensure reads are old.
            delayTarget_[i]=std::clamp(baseDelay_[i]*sizeScale_,100.f,float(lines_[i].size())-64.f);
            gainsTarget_[i]=std::exp(-6.907755f*delayTarget_[i]/(sr_*rt60));
            gSum+=gainsTarget_[i];
        }
        // Energy normalisation: a diffuse field of N lossy circulations stores
        // 1/(1-g^2) of the injected energy. Scaling the injection by sqrt(1-g^2)
        // keeps the wet level steady for any DECAY / SIZE.
        const float gMean=gSum*(1.f/16.f);
        const float oneMinusG2=std::max(.0004f,1.f-gMean*gMean);
        injectTarget_=std::clamp(kWetTarget*std::sqrt(2.f*oneMinusG2),.03f,1.2f);
        // The pitch return is normalised the same way, so its loop gain depends
        // only on PITCH RETURN and never grows with long DECAY / large SIZE.
        regenTarget_=kPitchRegen*std::sqrt(2.f*oneMinusG2);
        apGainTarget_=.28f+.49f*diffusion_;
        updateDampTarget();
    }
    void setPreDelayMs(float ms) {
        const float room=float(preL_.size())-sr_*.095f-8.f; // keep room for early taps
        preSamples_=size_t(std::clamp(ms*.001f*sr_,1.f,std::max(1.f,room)));
    }
    void setPitchReturnAmount(float x){targetGainPitch_=std::clamp(x,0.f,1.f);}
    void setDamping(float d){
        damping_=std::clamp(d,0.f,1.f);
        updateDampTarget();
    }
    void setMultiPitch(float bSemis,float bGain,float grainMs,float tone,float aGain,float width){
        targetRatioB_=std::pow(2.,double(std::clamp(bSemis,-24.f,24.f))/12.);
        gainA_=std::clamp(aGain,0.f,1.f);gainB_=std::clamp(bGain,0.f,1.f);
        grainSpanTarget_=std::clamp(sr_*std::clamp(grainMs,45.f,210.f)*.001f,
                512.f,float(grainL_.size())-128.f);
        width_=std::clamp(width*2.f,0.f,2.f);
        updateWidth();
        const float f=300.f*std::pow(55.f,std::clamp(tone,0.f,1.f));
        toneAlpha_=1.f-std::exp(-2.f*PI_F*std::min(f,sr_*.45f)/sr_);
        multi_=true;
    }
    void setMultiPitchLegacy(){
        multi_=false;gainA_=1.f;gainB_=.2f;width_=1.f;updateWidth();
        targetRatioB_=targetRatioA_>1.05?targetRatioA_*.5:1.5;
        grainSpanTarget_=std::min(sr_*.125f,float(grainL_.size())-128.f);
        toneAlpha_=1.f-std::exp(-2.f*PI_F*2800.f/sr_);
    }
    void setAtmosphere(float bloom,bool freeze){
        bloom_=std::clamp(bloom,0.f,1.f);
        freezeTarget_=freeze?1.f:0.f;
    }

    inline void processStereo(float& left,float& right){
        if(preL_.empty())return;
        const float dryL=left,dryR=right;
        // ---- control smoothing: parameter moves never click -------------------
        constexpr float kMaxSlew=.05f; // samples of delay change per sample (~5 % pitch)
        for(int i=0;i<16;++i){
            float d=delayTarget_[i]-delay_[i];
            d=d>kMaxSlew?kMaxSlew:(d<-kMaxSlew?-kMaxSlew:d);
            delay_[i]+=d;
            gains_[i]+=(gainsTarget_[i]-gains_[i])*gainCoeff_;
        }
        for(int k=0;k<kEarlyTaps;++k)for(int c=0;c<2;++c){
            float d=erTarget_[k][c]-erPos_[k][c];
            d=d>kMaxSlew?kMaxSlew:(d<-kMaxSlew?-kMaxSlew:d);
            erPos_[k][c]+=d;
        }
        injectGain_+=(injectTarget_-injectGain_)*gainCoeff_;
        regenGain_+=(regenTarget_-regenGain_)*gainCoeff_;
        apGain_+=(apGainTarget_-apGain_)*gainCoeff_;
        dampAlpha_+=(dampTarget_-dampAlpha_)*gainCoeff_;
        bloomS_+=(bloom_-bloomS_)*gainCoeff_;
        freezeAmt_+=(freezeTarget_-freezeAmt_)*freezeCoeff_;
        const float live=1.f-freezeAmt_;

        const size_t psize=preL_.size();
        preL_[preWrite_]=left;preR_[preWrite_]=right;
        const size_t read=(preWrite_+psize-preSamples_)%psize;
        float inL=preL_[read]*live;
        float inR=preR_[read]*live;
        // Early reflections: tap network on the pre-delayed signal with soft
        // L/R cross-feed (as in the Hall), so the image is wide yet coherent.
        float erL=0.f,erR=0.f;
        {
            const bool mono=reverbMode_==ReverbMode::Mono;
            float g=.62f;
            for(int k=0;k<kEarlyTaps;++k){
                const float sgn=(k&1)?-1.f:1.f;
                const float aL=readLine(preL_,preWrite_,float(preSamples_)+erPos_[k][0]);
                const float bR=readLine(preR_,preWrite_,float(preSamples_)+erPos_[k][1]);
                const float aR=readLine(preR_,preWrite_,float(preSamples_)+erPos_[k][0]);
                const float bL=readLine(preL_,preWrite_,float(preSamples_)+erPos_[k][1]);
                if(mono){const float m=.5f*(aL+aR+bL+bR);erL+=g*sgn*m;erR+=g*sgn*m;}
                else{
                    erL+=g*sgn*(.78f*aL+.22f*aR);
                    erR+=g*sgn*(.78f*bR+.22f*bL);
                }
                g*=.86f;
            }
            erL*=live;erR*=live;
        }
        if(++preWrite_>=psize)preWrite_=0;
        if(reverbMode_==ReverbMode::Mono)inL=inR=.5f*(inL+inR);
        // Inaudible (-360 dB) alternating offset keeps every recursive state out of
        // the denormal range during long silences.
        denormSign_=-denormSign_;
        const float tiny=1.e-18f*denormSign_;
        inL+=tiny;inR-=tiny;
        for(int s=0;s<4;++s){
            inL=allpass(inL,apL_[s],apPosL_[s],apGain_);
            inR=allpass(inR,apR_[s],apPosR_[s],apGain_);
        }
        // BLOOM adds two more diffusion stages: denser, smoother build-up.
        float denseL=inL,denseR=inR;
        for(int s=4;s<kApStages;++s){
            denseL=allpass(denseL,apL_[s],apPosL_[s],apGain_);
            denseR=allpass(denseR,apR_[s],apPosR_[s],apGain_);
        }
        const float bl=bloomS_;
        const float tankInL=inL+(denseL-inL)*bl;
        const float tankInR=inR+(denseR-inR)*bl;

        // Freeze bypasses the in-loop damping filter so nothing darkens or fades.
        const float alpha=dampAlpha_+(1.f-dampAlpha_)*freezeAmt_;
        // Low-frequency decay shelf: bass circulates with less energy than the
        // mids, avoiding the mud that long tails otherwise accumulate.
        const float lfCut=(1.f-kLfRatio)*(1.f-freezeAmt_);
        const float md=modDepth_*sr_*.0015f;
        float taps[16];float sum=0.f;
        // Read the previous circulation; writes follow after the orthogonal mix.
        for(int i=0;i<16;++i){
            // Complex-oscillator LFO (rotation + amplitude correction).
            const float nc=modC_[i]*modCr_[i]-modS_[i]*modSr_[i];
            const float ns=modS_[i]*modCr_[i]+modC_[i]*modSr_[i];
            const float k=1.5f-.5f*(nc*nc+ns*ns);
            modC_[i]=nc*k;modS_[i]=ns*k;
            const float x=readLine(lines_[i],pos_[i],delay_[i]+md*modS_[i]);
            lp_[i]+=alpha*(x-lp_[i]);
            lfState_[i]+=lfCoeff_*(lp_[i]-lfState_[i]);
            taps[i]=lp_[i]-lfCut*lfState_[i];
            sum+=taps[i];
        }
        float wetL=0.f,wetR=0.f;
        for(int i=0;i<8;++i){
            wetL+=(i%2?-1.f:1.f)*taps[i];
            wetR+=(i%2?1.f:-1.f)*taps[i+8];
        }
        wetL*=.25f;wetR*=.25f;
        grainL_[grainWrite_]=loopClip(wetL);
        grainR_[grainWrite_]=loopClip(wetR);
        ratioA_+=(targetRatioA_-ratioA_)*ampCoeff_;
        ratioB_+=(targetRatioB_-ratioB_)*ampCoeff_;
        grainSpan_+=(grainSpanTarget_-grainSpan_)*grainCoeff_;
        gainPitch_+=(targetGainPitch_-gainPitch_)*ampCoeff_;
        float pL=0.f,pR=0.f;
        if(pitchMode_!=PitchMode::Bypass){
            const bool rev=pitchMode_==PitchMode::SingleReverse||pitchMode_==PitchMode::DualReverse;
            pL=grainRead(grainL_,grainPhaseA_[0],ratioA_,rev);
            pR=grainRead(grainR_,grainPhaseA_[1],ratioA_,rev);
            pL*=gainA_;pR*=gainA_;
            if(pitchMode_==PitchMode::Dual||pitchMode_==PitchMode::DualReverse){
                const float bL=grainRead(grainL_,grainPhaseB_[0],ratioB_,rev);
                const float bR=grainRead(grainR_,grainPhaseB_[1],ratioB_,rev);
                pL+=gainB_*bL;pR+=gainB_*bR;
                const float norm=1.f/std::max(1.f,gainA_+gainB_);
                pL*=norm;pR*=norm;
            }
        }
        if(++grainWrite_>=grainL_.size())grainWrite_=0;
        // A bandpass/filter in the pitch-return circuit, and an independent
        // Tone Shift low-pass on the audible harmonics.
        pitchLow_.processStereo(pL,pR);
        pitchHigh_.processStereo(pL,pR);
        pitchToneL_+=toneAlpha_*(pL-pitchToneL_);
        pitchToneR_+=toneAlpha_*(pR-pitchToneR_);
        pL=pitchToneL_;pR=pitchToneR_;

        // ---- write the tank --------------------------------------------------
        const float pitchInject=gainPitch_*regenGain_*live; // released while frozen
        const float orth=2.f/16.f*sum;
        for(int i=0;i<16;++i){
            // Householder is norm preserving.
            const float matrix=orth-taps[i];
            const float sampleIn=(i<8?tankInL:tankInR)*injectGain_+(i<8?pL:pR)*pitchInject;
            const float g=gains_[i]+(kHoldGain-gains_[i])*freezeAmt_;
            const float mixed=loopClip(g*matrix+sampleIn)+tiny;
            lines_[i][pos_[i]]=std::isfinite(mixed)?mixed:0.f;
            if(++pos_[i]>=lines_[i].size())pos_[i]=0;
        }
        // ---- wet return ------------------------------------------------------
        // Late field = decorrelated L/R taps + a coherent centre component, giving
        // a positive L/R correlation (solid centre, mono compatible) like the Hall.
        const float common=sum*kCommonScale;
        const float lateL=wetL*kLateSide+common*kLateCentre;
        const float lateR=wetR*kLateSide+common*kLateCentre;
        const float earlyGain=kEarly*(1.f-.85f*bl);
        float returnL=lateL+erL*earlyGain+pL*gainPitch_*kPitchOut;
        float returnR=lateR+erR*earlyGain+pR*gainPitch_*kPitchOut;
        wetLow_.processStereo(returnL,returnR);
        wetHigh_.processStereo(returnL,returnR);
        const float mid=.5f*(returnL+returnR),side=.5f*(returnL-returnR);
        returnL=mid+side*widthEff_;returnR=mid-side*widthEff_;
        // Equal-power near 0..1 with bounded output and no duplicate dry path.
        const float dryGain=std::cos(mix_*PI_F*.5f);
        const float wetGain=std::sin(mix_*PI_F*.5f);
        float outL=dryL*dryGain+outClip(returnL)*wetGain;
        float outR=dryR*dryGain+outClip(returnR)*wetGain;
        if(!std::isfinite(outL)||!std::isfinite(outR)){outL=dryL;outR=dryR;}
        left=outL;right=outR;
    }
private:
    static constexpr int kApStages=6;
    static constexpr int kEarlyTaps=8;
    // Calibration constants (steady-state wet at LEVEL 100 % is comparable to Hall).
    static constexpr float kWetTarget=1.65f;  // late-tank wet energy ~ input energy (like Hall)
    static constexpr float kEarly=.16f;       // early reflections (faded by BLOOM)
    static constexpr float kPitchOut=.80f;    // transposed voice in the wet output
    static constexpr float kPitchRegen=1.6f;  // loop gain of the transposed voice re-injected in the tank
    static constexpr float kHoldGain=.99995f; // loss-free circulation while frozen
    static constexpr float kLfCorner=160.f;   // Hz, bass decay shelf corner
    static constexpr float kLfRatio=.85f;     // bass gain per circulation relative to mids
    static constexpr float kLateSide=.72f;    // decorrelated part of the late field
    static constexpr float kLateCentre=.70f;  // coherent part of the late field
    static constexpr float kCommonScale=.18f; // 16 taps summed -> same RMS scale as wetL

    // Transparent below the knee; asymptotically bounded above it. Unlike a
    // permanently-compressing curve this adds no level-dependent decay.
    static float knee(float x,float th,float ceiling){
        const float a=std::abs(x);
        if(a<=th)return x;
        return std::copysign(th+(ceiling-th)*std::tanh((a-th)/(ceiling-th)),x);
    }
    static float loopClip(float x){return knee(x,1.f,2.f);}
    static float outClip(float x){return knee(x,.85f,1.25f);}
    static float allpass(float x,std::vector<float>& b,size_t& i,float g){
        const float z=b[i];const float y=z-g*x;b[i]=x+g*y;
        if(++i>=b.size())i=0;
        return y;
    }
    static float readLine(const std::vector<float>& buf,size_t write,float delay){
        const float n=float(buf.size());float index=float(write)-delay;
        if(index<0.f)index+=n;
        if(index>=n)index-=n;
        const size_t i=size_t(index),j=i+1>=buf.size()?0:i+1;
        const float frac=index-float(i);
        return buf[i]*(1.f-frac)+buf[j]*frac;
    }
    // 4-point Hermite: much lower aliasing / HF loss than linear interpolation
    // for the pitch-shifter read heads.
    static float readHermite(const std::vector<float>& b,size_t write,float delay){
        const int n=int(b.size());
        float index=float(write)-delay;
        while(index<0.f)index+=float(n);
        int i1=int(index);const float f=index-float(i1);
        if(i1>=n)i1-=n;
        const int i0=i1-1<0?n-1:i1-1;
        int i2=i1+1;if(i2>=n)i2-=n;
        int i3=i2+1;if(i3>=n)i3-=n;
        const float y0=b[i0],y1=b[i1],y2=b[i2],y3=b[i3];
        const float c1=.5f*(y2-y0);
        const float c2=y0-2.5f*y1+2.f*y2-.5f*y3;
        const float c3=.5f*(y3-y0)+1.5f*(y1-y2);
        return ((c3*f+c2)*f+c1)*f+y1;
    }
    float grainRead(const std::vector<float>& b,double& phase,double ratio,bool reverse){
        const float span=grainSpan_;
        const float delta=float((reverse?1.+ratio:1.-ratio)/double(span));
        // Min delay + 0..span; both Hann read heads overlap with unit sum.
        const float minimum=sr_*.004f;
        const float pos1=float(phase),pos2=std::fmod(pos1+.5f,1.f);
        const float s1=readHermite(b,grainWrite_,minimum+span*pos1);
        const float s2=readHermite(b,grainWrite_,minimum+span*pos2);
        const int i1=std::min(1023,int(pos1*1024.f));
        const int i2=std::min(1023,int(pos2*1024.f));
        const float w1=window_[i1],w2=window_[i2];
        const float out=(s1*w1+s2*w2)/(w1+w2+1.e-5f);
        phase+=delta;phase-=std::floor(phase);
        return out;
    }
    void updateWidth(){widthEff_=std::clamp(width_*modeWidth_,0.f,2.2f);}
    void updateDampTarget(){
        const float cutoff=(color_==ColorMode::Dark?3500.f:13000.f)*
            std::pow(1.f-std::clamp(damping_,0.f,1.f)*.82f,.9f);
        dampTarget_=1.f-std::exp(-2.f*PI_F*std::clamp(cutoff,250.f,sr_*.44f)/sr_);
    }
    // Used by init(): start from the target values instead of gliding to them.
    void snapToTargets(){
        for(int i=0;i<16;++i){delay_[i]=delayTarget_[i];gains_[i]=gainsTarget_[i];lp_[i]=0.f;lfState_[i]=0.f;}
        for(int k=0;k<kEarlyTaps;++k)for(int c=0;c<2;++c)erPos_[k][c]=erTarget_[k][c];
        injectGain_=injectTarget_;regenGain_=regenTarget_;apGain_=apGainTarget_;dampAlpha_=dampTarget_;
        bloomS_=bloom_;freezeAmt_=freezeTarget_;
        ratioA_=targetRatioA_;ratioB_=targetRatioB_;gainPitch_=targetGainPitch_;
        grainSpan_=grainSpanTarget_;
    }
    float sr_=48000.f;
    std::array<std::vector<float>,16> lines_{};
    std::array<float,16> baseDelay_{},delay_{},delayTarget_{},gains_{},gainsTarget_{},lp_{},lfState_{};
    std::array<float,16> modC_{},modS_{},modCr_{},modSr_{};
    std::array<size_t,16> pos_{};
    std::vector<float> preL_,preR_,grainL_,grainR_;
    std::array<std::vector<float>,kApStages> apL_{},apR_{};
    std::array<size_t,kApStages> apPosL_{},apPosR_{};
    std::array<float,1024> window_{};
    float erPos_[kEarlyTaps][2]{},erTarget_[kEarlyTaps][2]{};
    size_t preWrite_=0,grainWrite_=0,preSamples_=1;
    double grainPhaseA_[2]{},grainPhaseB_[2]{};
    double ratioA_=2.,targetRatioA_=2.,ratioB_=1.5,targetRatioB_=1.5;
    float grainSpan_=6000.f,grainSpanTarget_=6000.f,grainCoeff_=.001f;
    float gainPitch_=0.f,targetGainPitch_=0.f,ampCoeff_=.001f,gainCoeff_=.001f,freezeCoeff_=.001f,lfCoeff_=.02f;
    float gainA_=1.f,gainB_=.3f,width_=1.f,modeWidth_=1.f,widthEff_=1.f;
    float pitchToneL_=0.f,pitchToneR_=0.f,toneAlpha_=.5f;
    float feedback_=.75f,diffusion_=.85f,size_=.8f,sizeScale_=1.2f;
    float mix_=1.f,modRate_=.4f,modDepth_=.1f;
    float damping_=.35f,dampAlpha_=.5f,dampTarget_=.5f,apGain_=.5f,apGainTarget_=.5f,bloom_=.5f,bloomS_=.5f;
    float injectGain_=.2f,injectTarget_=.2f,regenGain_=.2f,regenTarget_=.2f,freezeAmt_=0.f,freezeTarget_=0.f,denormSign_=1.f;
    bool multi_=false;
    PitchMode pitchMode_=PitchMode::Single;
    ReverbMode reverbMode_=ReverbMode::BigStereo;
    ColorMode color_=ColorMode::Bright;
    StereoBiquad wetLow_,wetHigh_,pitchLow_,pitchHigh_;
};
