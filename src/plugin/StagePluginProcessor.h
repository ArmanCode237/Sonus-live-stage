#pragma once
// v33 (V1/V2): the VST3 / AU plug-in hosts the SAME core as the app (StageApp: programs,
// presets, library, browser, mappings and the HTTP / WebSocket API) on a private loopback
// port, and its editor shows the same console in an embedded WebView2 / WKWebView.
// Audio and MIDI come from the DAW (makeHostedAudioBackend); MIDI is interpreted by the
// shared dispatchMidiMessage, exactly like the app's RtMidi inputs.
#include "app/StageApp.h"      // first: httplib must precede <windows.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

class StagePluginProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    StagePluginProcessor();
    ~StagePluginProcessor() override;
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Sonus Live Stage"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    // The 8 global programs of Sonus Live Stage (same bank as the app).
    int getNumPrograms() override { return StageApp::programCount(); }
    int getCurrentProgram() override { return app_ ? app_->activeProgram() : 0; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    SonusDSP::WorkstationHost& engine() { return app_->getEngine(); }
    /** Console URL with this instance's session token ("" if the server could not start). */
    juce::String consoleUrl() const;
    bool serverRunning() const { return serverOk_; }

private:
    // v33 (V3) DAW automation: a fixed set of engine parameters exposed to the host.
    struct AutomatedParam { std::string id; juce::AudioParameterFloat* param = nullptr; float lastApplied = 0.f; };
    void createAutomationParameters();
    void timerCallback() override;     // 60 Hz on the message thread: DAW <-> engine
    std::vector<AutomatedParam> automated_;
    uint64_t lastRevision_ = 0;

    std::unique_ptr<StageApp> app_;
    bool serverOk_ = false;
    std::vector<float> rendered_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StagePluginProcessor)
};
