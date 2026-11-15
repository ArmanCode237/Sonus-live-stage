#include "StagePluginProcessor.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
using json=nlohmann::json;
namespace {
class StageEditor final : public juce::AudioProcessorEditor {
    juce::Label header_,hint_;
public:
    explicit StageEditor(StagePluginProcessor& proc):juce::AudioProcessorEditor(proc){
        setSize(520,180);
        header_.setText("LIVE STAGE · VST3 / AU",juce::dontSendNotification);
        header_.setFont(juce::Font(juce::FontOptions(23.f,juce::Font::bold)));
        header_.setJustificationType(juce::Justification::centred);addAndMakeVisible(header_);
        hint_.setText("DSP de 4 capas · MIDI · Factory 1–256. Editor avanzado en desarrollo.",juce::dontSendNotification);
        hint_.setJustificationType(juce::Justification::centred);addAndMakeVisible(hint_);
    }
    void resized() override {header_.setBounds(10,25,getWidth()-20,55);hint_.setBounds(8,88,getWidth()-16,55);}
    void paint(juce::Graphics& g) override {g.fillAll(juce::Colour::fromRGB(18,22,30));}
};
}
StagePluginProcessor::StagePluginProcessor():juce::AudioProcessor(BusesProperties().withOutput("Stereo",juce::AudioChannelSet::stereo(),true)){setCurrentProgram(0);}
void StagePluginProcessor::prepareToPlay(double sampleRate,int samplesPerBlock){
    engine_.setSampleRate(float(sampleRate));
    rendered_.assign(size_t(std::max(8192,samplesPerBlock))*2,0.f);
}
void StagePluginProcessor::releaseResources(){engine_.allNotesOff();}
bool StagePluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts)const{
    return layouts.getMainOutputChannelSet()==juce::AudioChannelSet::stereo() &&
        layouts.getMainInputChannelSet().isDisabled();
}
void StagePluginProcessor::processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi){
    juce::ScopedNoDenormals guard;
    audio.clear();
    const int frames=audio.getNumSamples();if(frames<=0)return;
    // No heap growth in processBlock; render oversized host buffers in chunks.
    // Split at MIDI sample positions for note-accurate triggering.
    auto renderSegment=[&](int begin,int end){if(end<=begin)return;
        while(begin<end){
            const int count=std::min(end-begin,int(rendered_.size()/2));
            engine_.render(rendered_.data(),unsigned(count));
            for(int i=0;i<count;i++)for(int ch=0;ch<2;ch++)
                audio.setSample(ch,begin+i,rendered_[size_t(i)*2+ch]);
            begin+=count;
        }};
    int cursor=0;
    for(const auto metadata:midi){
        int pos=std::clamp(metadata.samplePosition,0,frames);
        renderSegment(cursor,pos);cursor=pos;
        const auto msg=metadata.getMessage();
        if(msg.isNoteOn())engine_.noteOn(msg.getNoteNumber(),juce::jlimit(1,127,int(msg.getVelocity()*127.f)));
        else if(msg.isNoteOff())engine_.noteOff(msg.getNoteNumber());
        else if(msg.isSustainPedalOn())engine_.setSustainPedal(true);
        else if(msg.isSustainPedalOff())engine_.setSustainPedal(false);
        else if(msg.isController()){
            const int cc=msg.getControllerNumber(),value=msg.getControllerValue();
            if(cc==1)engine_.setModWheel(value/127.f);
            if(!engine_.getMappings().handleMidi(NordDSP::MidiKind::CC,msg.getChannel(),cc,value)){
                if(cc==64)engine_.setSustainPedal(value>=64);
                else if(cc==7)engine_.setMasterGain(value/100.f);
                else if(cc==123)engine_.allNotesOff();
            }
        }else if(msg.isPitchWheel())engine_.setPitchBend((msg.getPitchWheelValue()-8192)/8192.f);
        else if(msg.isAllNotesOff()||msg.isAllSoundOff())engine_.allNotesOff();
    }
    renderSegment(cursor,frames);
}
juce::AudioProcessorEditor* StagePluginProcessor::createEditor(){return new StageEditor(*this);}
void StagePluginProcessor::setCurrentProgram(int idx){
    currentFactoryPreset_=std::clamp(idx+1,1,256);
    engine_.getRegistry().setParamValue("synthPresetId",float(currentFactoryPreset_));
    engine_.getRegistry().setParamValue("synthEnabled",1.f);
}
void StagePluginProcessor::getStateInformation(juce::MemoryBlock& out){
    auto p=engine_.getCurrentParams();
    json data={{"version",16},{"factoryPreset",currentFactoryPreset_},
        {"synthEnabled",p.synthEnabled},{"organEnabled",p.organEnabled},
        {"pianoEnabled",p.pianoEnabled},{"synthLayers",engine_.getSynthLayersJson()},
        {"organ",engine_.getOrganParamsJson()}};
    const std::string text=data.dump();out.append(text.data(),text.size());
}
void StagePluginProcessor::setStateInformation(const void* data,int size){
    if(!data||size<=0||size>2*1024*1024)return;
    auto j=json::parse(std::string(static_cast<const char*>(data),size),nullptr,false);
    if(!j.is_object())return;
    setCurrentProgram(std::clamp(j.value("factoryPreset",1)-1,0,255));
    if(j.contains("synthLayers"))engine_.applySynthPresetJson(json{{"layers",j["synthLayers"]}});
    if(j.contains("organ"))engine_.applyOrganPresetJson(j["organ"]);
    engine_.getRegistry().setParamValue("synthEnabled",j.value("synthEnabled",true)?1.f:0.f);
    engine_.getRegistry().setParamValue("organEnabled",j.value("organEnabled",false)?1.f:0.f);
    engine_.getRegistry().setParamValue("pianoEnabled",j.value("pianoEnabled",false)?1.f:0.f);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new StagePluginProcessor();}
