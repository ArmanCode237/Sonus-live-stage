// VST3/AU foundation, sharing Live Stage DSP. No HTTP/OSC service inside DAW.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/core/WorkstationHost.h"
#include <vector>
#include <algorithm>

class LiveStageInstrumentProcessor final : public juce::AudioProcessor {
public:
    LiveStageInstrumentProcessor()
      : juce::AudioProcessor(juce::AudioProcessor::BusesProperties()
          .withOutput("Stereo Out", juce::AudioChannelSet::stereo(), true)) {
        instrument.applyParamById("synthEnabled", 1.f);
        instrument.applyParamById("synthPresetId", 1.f);
    }
    const juce::String getName() const override { return "Live Stage Instrument"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 256; }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram(int index) override {
        currentPreset=juce::jlimit(0,255,index);
        instrument.applyParamById("synthPresetId",float(currentPreset+1));
    }
    const juce::String getProgramName(int index) override { return "Synth " + juce::String(index+1); }
    void changeProgramName(int,const juce::String&) override {}
    void prepareToPlay(double sampleRate,int maximumBlockSize) override {
        instrument.setSampleRate(sampleRate);
        interleaved.assign(size_t(std::max(2,maximumBlockSize))*2,0.f);
    }
    void releaseResources() override {instrument.allNotesOff();}
    bool isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& l) const override {
        return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();
        const int frames=buffer.getNumSamples();
        int rendered=0;
        // Preserve event sample offsets without allocating inside the audio thread.
        auto renderSegment=[&](int until){
            while(rendered<until){
                const int available=int(interleaved.size()/2);
                if(!available)return;
                const int n=std::min(until-rendered,available);
                instrument.render(interleaved.data(),static_cast<unsigned int>(n));
                float* l=buffer.getWritePointer(0),*r=buffer.getWritePointer(1);
                for(int i=0;i<n;++i){l[rendered+i]=interleaved[size_t(i)*2];r[rendered+i]=interleaved[size_t(i)*2+1];}
                rendered+=n;
            }
        };
        for(const auto metadata:midi){
            const auto message=metadata.getMessage();
            renderSegment(juce::jlimit(rendered,frames,metadata.samplePosition));
            if(message.isNoteOn())instrument.noteOn(message.getNoteNumber(),int(message.getVelocity()));
            else if(message.isNoteOff())instrument.noteOff(message.getNoteNumber());
            else if(message.isController()){
                const int cc=message.getControllerNumber(),value=message.getControllerValue();
                if(!instrument.getMappings().handleMidi(NordDSP::MidiKind::CC,message.getChannel(),cc,value)){
                    if(cc==1)instrument.setModWheel(float(value)/127.f);
                    if(cc==64)instrument.setSustainPedal(value>=64);
                    if(cc==121){instrument.setModWheel(0);instrument.setPitchBend(0);}
                }
            }else if(message.isPitchWheel()){
                const int bend=message.getPitchWheelValue();
                instrument.setPitchBend(bend>=8192?float(bend-8192)/8191.f:float(bend-8192)/8192.f);
            }else if(message.isAllNotesOff()||message.isAllSoundOff())instrument.allNotesOff();
        }
        renderSegment(frames);
        midi.clear();
    }
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override {
        return new juce::GenericAudioProcessorEditor(*this);
    }
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::MemoryOutputStream stream(dest,true);
        stream.writeInt(currentPreset);
        stream.writeInt(1); // state version
    }
    void setStateInformation(const void* bytes,int size) override {
        juce::MemoryInputStream stream(bytes,static_cast<size_t>(std::max(0,size)),false);
        if(size>=4)setCurrentProgram(stream.readInt());
    }
private:
    NordDSP::WorkstationHost instrument;
    std::vector<float> interleaved;
    int currentPreset=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiveStageInstrumentProcessor)
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new LiveStageInstrumentProcessor();}
