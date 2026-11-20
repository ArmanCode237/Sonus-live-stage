#include "StagePluginProcessor.h"
#include "app/AudioBackend.h"
#include "app/MidiInput.h"
#include "app/ParamSerialization.h"
#include "control/ParamValueReader.h"
#include "app/UserDataPaths.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#if JUCE_MAC
#include <dlfcn.h>
#endif

namespace {

/** Folder with ui/ and presets/: the bundle's Contents/Resources (VST3 on Windows and macOS,
 *  AU on macOS). STAGE_DEV_ASSET_ROOT (the source tree) is the fallback for local builds. */
std::string findPluginAssets() {
    // On macOS currentExecutableFile is often the DAW, NOT this VST3 binary.
    // dladdr resolves the actual image containing this function.
#if JUCE_MAC
    Dl_info image{};
    if (!dladdr(reinterpret_cast<const void*>(&findPluginAssets), &image) || !image.dli_fname)
        return {};
    const auto binary = juce::File(juce::String::fromUTF8(image.dli_fname));
#else
    const auto binary = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
#endif
    // <name>.vst3/Contents/x86_64-win/<name>.vst3   ·   <name>.vst3|component/Contents/MacOS/<name>
    for (auto dir = binary.getParentDirectory(); dir.exists() && !dir.isRoot(); dir = dir.getParentDirectory()) {
        const auto resources = dir.getChildFile("Resources");
        if (resources.getChildFile("ui").isDirectory()) return resources.getFullPathName().toStdString();
        if (dir.getFileName() == "Contents") break;
    }
#ifdef STAGE_DEV_ASSET_ROOT
    if (juce::File(STAGE_DEV_ASSET_ROOT).getChildFile("ui").isDirectory()) return STAGE_DEV_ASSET_ROOT;
#endif
    return {};
}

// ---------------------------------------------------------------- editor
// The same console as the app, in WebView2 (Windows) / WKWebView (macOS), loaded from this
// instance's private loopback server with its session token.
class StageEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit StageEditor(StagePluginProcessor& proc) : juce::AudioProcessorEditor(proc), proc_(proc) {
        const auto url = proc.consoleUrl();
        auto options = juce::WebBrowserComponent::Options{}
            .withKeepPageLoadedWhenBrowserIsHidden();
#if JUCE_WINDOWS
        options = options
            .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2{}
                .withUserDataFolder(juce::File(stageUserDataDir().string()).getChildFile("webview2-plugin"))
                .withBackgroundColour(juce::Colour::fromRGB(18, 20, 24))
                .withStatusBarDisabled());
#endif
        // macOS deliberately uses JUCE's default WKWebView (no WebView2 dependency).
        if (url.isNotEmpty() && juce::WebBrowserComponent::areOptionsSupported(options)) {
            browser_ = std::make_unique<juce::WebBrowserComponent>(options);
            addAndMakeVisible(*browser_);
            browser_->goToURL(url);
        } else {
            // Browser unavailable or local server could not start: offer a fallback.
            message_.setText(url.isEmpty()
                ? "Sonus Live Stage no pudo abrir su servidor local. Reabre el plug-in o reinicia el DAW."
#if JUCE_MAC
                : "No se pudo crear la vista WebKit en el DAW.\n"
                  "Puedes abrir la consola en tu navegador mientras tanto.", juce::dontSendNotification);
#else
                : "Falta Microsoft Edge WebView2 Runtime para mostrar la consola dentro del DAW.\n"
                  "Puedes abrirla en tu navegador mientras tanto.", juce::dontSendNotification);
#endif
            message_.setJustificationType(juce::Justification::centred);
            message_.setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(message_);
            if (url.isNotEmpty()) {
                openInBrowser_.setButtonText("ABRIR EN EL NAVEGADOR");
                openInBrowser_.onClick = [url] { juce::URL(url).launchInDefaultBrowser(); };
                addAndMakeVisible(openInBrowser_);
            }
        }
        // Size last: resized() must see the browser (sizing first left it at 0 x 0).
        setResizable(true, true);
        setResizeLimits(960, 600, 3840, 2400);
        setSize(1440, 900);
        startTimerHz(1);
    }
    void resized() override {
        if (browser_) { browser_->setBounds(getLocalBounds()); return; }
        auto r = getLocalBounds().reduced(24);
        message_.setBounds(r.removeFromTop(r.getHeight() / 2));
        openInBrowser_.setBounds(r.removeFromTop(40).withSizeKeepingCentre(260, 40));
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour::fromRGB(18, 20, 24)); }
private:
    void timerCallback() override {}   // reserved: reconnect / zoom follow-up (V3)
    StagePluginProcessor& proc_;
    std::unique_ptr<juce::WebBrowserComponent> browser_;
    juce::Label message_;
    juce::TextButton openInBrowser_;
};

} // namespace

// ---------------------------------------------------------------- processor
StagePluginProcessor::StagePluginProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Stereo", juce::AudioChannelSet::stereo(), true)) {
    if (const auto assets = findPluginAssets(); !assets.empty()) setStageAssetRoot(assets);
    StageAppOptions options;
    options.plugin = true;
    options.makeBackend = makeHostedAudioBackend;
    char appName[] = "LiveStagePlugin";
    char* argv[] = { appName, nullptr };
    app_ = std::make_unique<StageApp>(1, argv, std::move(options));
    // Loopback only, any free port: several instances (tracks) can run at once.
    serverOk_ = app_->startServices("127.0.0.1", 0);
    createAutomationParameters();
    startTimerHz(60);
}

// ---------------------------------------------------------------- v33 (V3) automation
// Stable IDs (never rename: DAW projects reference them). Ranges come from the engine.
namespace {
struct AutoSpec { const char* id; const char* name; };
const AutoSpec kAutomation[] = {
    {"volume", "Master Volume"}, {"pianoVolume", "Piano Level"}, {"synthMasterVol", "Synth Level"}, {"organVolume", "Organ Level"},
    {"pianoLayerVol0", "Piano L1 Level"}, {"pianoLayerVol1", "Piano L2 Level"}, {"pianoLayerVol2", "Piano L3 Level"},
    {"pianoLayerVol3", "Piano L4 Level"}, {"pianoLayerVol4", "Piano L5 Level"},
    {"synthMacroCutoff", "Synth Timbre"}, {"synthMacro1", "Synth Macro 1"}, {"synthMacro2", "Synth Macro 2"},
    {"synthMacro3", "Synth Macro 3"}, {"synthMacro4", "Synth Macro 4"},
    {"organDrive", "Organ Drive"}, {"organRotaryFast", "Organ Rotary Fast"},
    {"drawbar0", "Drawbar 16'"}, {"drawbar1", "Drawbar 5 1/3'"}, {"drawbar2", "Drawbar 8'"}, {"drawbar3", "Drawbar 4'"},
    {"drawbar4", "Drawbar 2 2/3'"}, {"drawbar5", "Drawbar 2'"}, {"drawbar6", "Drawbar 1 3/5'"}, {"drawbar7", "Drawbar 1 1/3'"},
    {"drawbar8", "Drawbar 1'"}, {"reverbMix", "Reverb Mix"}, {"delayMix", "Delay Mix"},
};
}

void StagePluginProcessor::createAutomationParameters() {
    auto& registry = engine().getRegistry();
    const json params = serializeParams(engine().getCurrentParams());
    for (const auto& spec : kAutomation) {
        float lo = 0.f, hi = 1.f;
        if (!registry.paramRange(spec.id, lo, hi) || hi <= lo) continue;
        const float current = std::clamp(SonusDSP::readParamValue(params, spec.id).value_or(lo), lo, hi);
        auto* p = new juce::AudioParameterFloat(juce::ParameterID{spec.id, 1}, spec.name, juce::NormalisableRange<float>(lo, hi), current);
        addParameter(p);                                   // owned by the processor
        automated_.push_back({spec.id, p, current});
    }
}

void StagePluginProcessor::timerCallback() {
    if (!app_) return;
    auto& registry = engine().getRegistry();
    // DAW -> engine: automation and host-side edits.
    for (auto& a : automated_) {
        const float v = a.param->get();
        if (std::abs(v - a.lastApplied) > 1.0e-6f) { registry.setParamValue(a.id, v); a.lastApplied = v; }
    }
    // Engine -> DAW: edits in the console / by MIDI, program and preset loads.
    const uint64_t revision = app_->stateRevision();
    if (revision == lastRevision_) return;
    lastRevision_ = revision;
    const json params = serializeParams(engine().getCurrentParams());
    for (auto& a : automated_) {
        const auto value = SonusDSP::readParamValue(params, a.id);
        if (!value) continue;
        const float v = juce::jlimit(a.param->range.start, a.param->range.end, *value);
        if (std::abs(v - a.param->get()) > 1.0e-4f * (a.param->range.end - a.param->range.start)) {
            a.lastApplied = v;                            // do not echo it back to the engine
            a.param->setValueNotifyingHost(a.param->convertTo0to1(v));
        }
    }
}

StagePluginProcessor::~StagePluginProcessor() {
    stopTimer();
    if (app_) app_->stopServices();
}

juce::String StagePluginProcessor::consoleUrl() const {
    return serverOk_ && app_ ? juce::String(app_->consoleUrl()) : juce::String();
}

void StagePluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine().setSampleRate(float(sampleRate));
    rendered_.assign(size_t(std::max(8192, samplesPerBlock)) * 2, 0.f);
}

void StagePluginProcessor::releaseResources() { engine().allNotesOff(); }

bool StagePluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
           layouts.getMainInputChannelSet().isDisabled();
}

void StagePluginProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals guard;
    audio.clear();
    const int frames = audio.getNumSamples();
    if (frames <= 0) return;
    auto& eng = engine();
    const auto begin = std::chrono::steady_clock::now();
    // No heap growth in processBlock; render oversized host buffers in chunks.
    // Split at MIDI sample positions for note-accurate triggering.
    auto renderSegment = [&](int from, int to) {
        while (from < to) {
            const int count = std::min(to - from, int(rendered_.size() / 2));
            eng.render(rendered_.data(), unsigned(count));
            for (int ch = 0; ch < 2; ++ch) {
                float* out = audio.getWritePointer(ch, from);
                for (int i = 0; i < count; ++i) out[i] = rendered_[size_t(i) * 2 + size_t(ch)];
            }
            from += count;
        }
    };
    int cursor = 0;
    for (const auto metadata : midi) {
        const int pos = std::clamp(metadata.samplePosition, 0, frames);
        renderSegment(cursor, pos);
        cursor = pos;
        // Same interpretation as the app's MIDI inputs (notes, pedal, wheel, bend, Program
        // Change filter, mappings, clock). Source 0: the DAW track.
        dispatchMidiMessage(eng, metadata.data, size_t(metadata.numBytes), kMidiSourceInternal);
    }
    renderSegment(cursor, frames);
    // DSP-load telemetry, like the app's audio callback.
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count();
    eng.recordAudioCallback(uint64_t(std::max<int64_t>(0, elapsed)), unsigned(frames), false);
}

juce::AudioProcessorEditor* StagePluginProcessor::createEditor() { return new StageEditor(*this); }

void StagePluginProcessor::setCurrentProgram(int index) { if (app_) app_->requestProgram(index); }

const juce::String StagePluginProcessor::getProgramName(int index) {
    return app_ ? juce::String(app_->programSlotName(index)) : juce::String("Program ") + juce::String(index + 1);
}

// The DAW project stores the whole setup (piano layers with their samples, synth, organ,
// FX, mappings, Sounds and preset names): the same entry as a STORED program.
void StagePluginProcessor::getStateInformation(juce::MemoryBlock& out) {
    const json data = {{"format", "live-stage-plugin"}, {"version", 33}, {"state", app_->captureState()}};
    const std::string text = data.dump();
    out.append(text.data(), text.size());
}

void StagePluginProcessor::setStateInformation(const void* data, int size) {
    if (!data || size <= 0 || size > 64 * 1024 * 1024) return;
    const auto j = json::parse(std::string(static_cast<const char*>(data), size_t(size)), nullptr, false);
    if (!j.is_object()) return;
    if (j.contains("state")) { app_->restoreState(j["state"]); return; }
    // v16 projects (before v33): factory synth patch + synth / organ layers.
    auto& eng = engine();
    eng.getRegistry().setParamValue("synthPresetId", float(std::clamp(j.value("factoryPreset", 1), 1, 256)));
    if (j.contains("synthLayers")) eng.applySynthPresetJson(json{{"layers", j["synthLayers"]}});
    if (j.contains("organ")) eng.applyOrganPresetJson(j["organ"]);
    eng.getRegistry().setParamValue("synthEnabled", j.value("synthEnabled", true) ? 1.f : 0.f);
    eng.getRegistry().setParamValue("organEnabled", j.value("organEnabled", false) ? 1.f : 0.f);
    eng.getRegistry().setParamValue("pianoEnabled", j.value("pianoEnabled", false) ? 1.f : 0.f);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StagePluginProcessor(); }
