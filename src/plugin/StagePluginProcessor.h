#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "../core/WorkstationHost.h"

// Native JUCE VST3/AU adapter for the same WorkstationHost DSP.
// Independent of RtAudio/RtMidi and of the embedded HTTP server.
class StagePluginProcessor final : public juce::AudioProcessor {
public:
    StagePluginProcessor();
    void prepareToPlay(double sampleRate,int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override {return true;}
    const juce::String getName() const override {return "Live Stage";}
    bool acceptsMidi() const override {return true;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 8.0;}
    int getNumPrograms() override {return 256;}
    int getCurrentProgram() override {return currentFactoryPreset_-1;}
    void setCurrentProgram(int idx) override;
    const juce::String getProgramName(int idx) override {return "Factory "+juce::String(idx+1);}
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data,int size) override;
    NordDSP::WorkstationHost& engine(){return engine_;}
private:
    NordDSP::WorkstationHost engine_;
    int currentFactoryPreset_=1;
    std::vector<float> rendered_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StagePluginProcessor)
};
