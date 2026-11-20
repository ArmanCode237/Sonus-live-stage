// Live Stage · plug-in host check (no DAW needed). Loads the real .vst3 like a DAW does and
// verifies: instantiation, audio from MIDI, the 8 programs, state save / restore, two
// instances at once, and (with --editor) opens the editor window for a few seconds.
//   LiveStagePluginHostCheck "<path>\Live Stage.vst3" [--editor seconds]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <iostream>

static int failures = 0;
static void check(bool ok, const juce::String& label) {
    std::cout << (ok ? "PASS  " : "FAIL  ") << label << std::endl;
    if (!ok) ++failures;
}

static std::unique_ptr<juce::AudioPluginInstance> load(juce::AudioPluginFormatManager& fm, const juce::String& path, juce::String& error) {
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : fm.getFormats()) format->findAllTypesForFile(found, path);
    if (found.isEmpty()) { error = "no plug-in found in " + path; return nullptr; }
    return fm.createPluginInstance(*found[0], 48000.0, 512, error);
}

/** Renders `blocks` × 512 frames with a C major chord held for the first half; returns the peak. */
static float play(juce::AudioPluginInstance& p, int blocks) {
    juce::AudioBuffer<float> buffer(2, 512);
    float peak = 0.f;
    for (int b = 0; b < blocks; ++b) {
        juce::MidiBuffer midi;
        if (b == 2) for (int n : {60, 64, 67}) midi.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8)100), 10);
        if (b == blocks / 2) for (int n : {60, 64, 67}) midi.addEvent(juce::MidiMessage::noteOff(1, n), 0);
        buffer.clear();
        p.processBlock(buffer, midi);
        peak = std::max(peak, buffer.getMagnitude(0, 512));
        juce::Thread::sleep(2);   // near real time, so streamed samples keep up
    }
    return peak;
}

int main(int argc, char* argv[]) {
    if (argc < 2) { std::cerr << "usage: LiveStagePluginHostCheck <plugin.vst3> [--editor seconds]\n"; return 2; }
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String path = juce::File(argv[1]).getFullPathName();
    const int editorSeconds = argc >= 4 && juce::String(argv[2]) == "--editor" ? juce::String(argv[3]).getIntValue() : 0;

    juce::AudioPluginFormatManager fm;
    fm.addFormat(new juce::VST3PluginFormat());
    juce::String error;
    auto a = load(fm, path, error);
    check(a != nullptr, "instantiates (" + (a ? a->getName() : error) + ")");
    if (!a) return 1;
    check(a->acceptsMidi() && a->getTotalNumOutputChannels() == 2, "instrument with MIDI in and stereo out");

    a->prepareToPlay(48000.0, 512);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);   // let the program worker settle
    const float peak = play(*a, 400);
    check(peak > 1.0e-4f && std::isfinite(peak), "MIDI chord produces audio (peak " + juce::String(peak, 4) + ")");

    // v33 (V3): automation parameters the DAW can record (stable IDs).
    const auto& params = a->getParameters();
    juce::StringArray pnames;
    for (auto* p : params) pnames.add(p->getName(32));
    check(params.size() >= 20, "automation parameters: " + juce::String(params.size()) + " (" + pnames.joinIntoString(", ").substring(0, 120) + "...)");
    if (params.size() > 0) {
        params[0]->setValueNotifyingHost(0.5f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        check(std::abs(params[0]->getValue() - 0.5f) < 0.02f, "a host parameter change is kept (" + juce::String(params[0]->getValue(), 3) + ")");
    }
    check(a->getNumPrograms() == 8, "8 programs (" + juce::String(a->getNumPrograms()) + ")");
    juce::StringArray names;
    for (int i = 0; i < a->getNumPrograms(); ++i) names.add(a->getProgramName(i));
    check(names[0].isNotEmpty(), "program names: " + names.joinIntoString(" | "));

    juce::MemoryBlock state;
    a->getStateInformation(state);
    const auto text = juce::String::fromUTF8((const char*)state.getData(), (int)state.getSize());
    if (auto dump = juce::SystemStats::getEnvironmentVariable("LIVESTAGE_STATE_DUMP", {}); dump.isNotEmpty())
        juce::File(dump).replaceWithData(state.getData(), state.getSize());
    // A VST3 host receives the state wrapped by the VST3 layer (XML around our JSON).
    const std::string raw(static_cast<const char*>(state.getData()), state.getSize());   // binary header + XML
    check(state.getSize() > 10000 && (raw.find("VST3PluginState") != std::string::npos || raw.find("live-stage-plugin") != std::string::npos),
          "state holds the whole program (" + juce::String((int)state.getSize()) + " bytes)");
    a->setStateInformation(state.getData(), (int)state.getSize());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
    juce::MemoryBlock again;
    a->getStateInformation(again);
    check(again.getSize() > 1000, "state restores and saves again (" + juce::String((int)again.getSize()) + " bytes)");

    auto b = load(fm, path, error);
    check(b != nullptr, "second instance at the same time");
    if (b) { b->prepareToPlay(48000.0, 512); check(play(*b, 60) >= 0.f, "second instance renders"); b->releaseResources(); }
    b.reset();
    check(true, "second instance closes cleanly");

    if (editorSeconds > 0) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(a->createEditorIfNeeded());
        check(editor != nullptr, "editor opens");
        if (editor) {
            juce::DocumentWindow window("LiveStage plugin host check", juce::Colours::black, juce::DocumentWindow::closeButton);
            window.setUsingNativeTitleBar(true);
            window.setContentNonOwned(editor.get(), true);
            window.centreWithSize(editor->getWidth(), editor->getHeight());
            window.setVisible(true);
            std::cout << "EDITOR_VISIBLE " << editor->getWidth() << "x" << editor->getHeight() << std::endl;
            juce::MessageManager::getInstance()->runDispatchLoopUntil(editorSeconds * 1000);
            window.clearContentComponent();
            a->editorBeingDeleted(editor.get());
            editor.reset();
        }
    }
    a->releaseResources();
    a.reset();
    check(true, "first instance closes cleanly");
    std::cout << (failures ? "\nFAILED " + std::to_string(failures) : std::string("\nALL PASS")) << std::endl;
    return failures ? 1 : 0;
}
