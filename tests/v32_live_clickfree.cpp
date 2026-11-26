// Assertions carry the checks: keep them in Release builds.
#undef NDEBUG
// v32 live-performance regression (no soundcard):
//  - organ controls (drawbars, volume, model, drive), voice budget changes, voice
//    stealing and all-notes-off never produce a discontinuity larger than a normal
//    key release;
//  - the shared voice pool gives the piano its voices while organ / synth are on;
//  - insert units and studio slots crossfade ON/OFF, ring out (spill-over) and cost
//    nothing once silent; a re-enabled echo never replays stale audio;
//  - modules switched OFF fade out, then are skipped;
//  - the master limiter keeps the output below -0.3 dBFS.
#include "modules/StagePianoEngine.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace SonusDSP;

static constexpr unsigned SR = 44100, BUF = 256;

static std::shared_ptr<PianoInstrument> sineBank(int id, double hz, float amp = .5f) {
    auto b = std::make_shared<PianoInstrument>();
    b->id = id; b->name = "v32-" + std::to_string(id); b->formatType = "NORD SAMPLE";
    StrokeSample s; s.frames = SR * 10; s.sampleRate = SR; s.actualRootMidi = 60; s.lokey = 0; s.hikey = 127;
    s.pcmL.resize(s.frames); s.pcmR.resize(s.frames);
    for (unsigned k = 0; k < s.frames; ++k)
        s.pcmL[k] = s.pcmR[k] = int16_t(std::lrint(32767.0 * amp * std::sin(2 * 3.141592653589793 * k * hz / SR)));
    s.sampleScale = 1.f / 32768.f; s.readyFrames->store(s.frames);
    b->strokes.push_back(std::move(s));
    return b;
}

// Continuous capture: the curvature check spans block boundaries.
struct Capture {
    std::vector<float> x;
    void render(StagePianoEngine& e, int blocks) {
        std::vector<float> b(BUF * 2);
        for (int i = 0; i < blocks; ++i) { e.render(b.data(), BUF); x.insert(x.end(), b.begin(), b.end()); }
    }
    // max |x[n] - 2x[n-1] + x[n-2]| (left channel) from frame `from`
    double curvature(size_t from = 0) const {
        double m = 0;
        for (size_t i = std::max<size_t>(from, 2) * 2; i < x.size(); i += 2)
            m = std::max(m, double(std::abs(x[i] - 2 * x[i - 2] + x[i - 4])));
        return m;
    }
    size_t frames() const { return x.size() / 2; }
    double peak(size_t from = 0) const { double m = 0; for (size_t i = from * 2; i < x.size(); ++i) m = std::max(m, double(std::abs(x[i]))); return m; }
};

static std::unique_ptr<StagePianoEngine> engine(int maxVoices = 128) {
    auto e = std::make_unique<StagePianoEngine>();
    e->setSampleRate(SR); e->setMaxVoices(maxVoices);
    for (int i = 0; i < PIANO_LAYER_COUNT; ++i) { e->setLayerInstrument(i, nullptr); e->setPianoLayerEnabled(i, false); }
    e->multiSynth_.synthEnabled = false;
    e->organModule_.organEnabled = false;
    // Isolate the sources: no factory-default rack effects (their tails are legitimate).
    for (int r = 0; r < STUDIO_RACK_COUNT; ++r) for (int k = 0; k < STUDIO_SLOTS; ++k) e->setStudioSlot(r, k, 0, 0);
    return e;
}
static void pureOrgan(StagePianoEngine& e) {
    auto& o = e.organModule_;
    o.organEnabled = true; o.organModel = 0; o.vibratoMode = 0; o.rotaryMix = 0; o.percussionEnabled = false;
    o.organDrive = 0; o.chorusEnabled = false;
    for (int d = 0; d < 9; ++d) o.drawbars[d] = d == 2 ? 1.f : 0.f;   // 8' only: pure sines
}

// The event may not bend the waveform more than a normal key release does.
static void expectSmooth(const char* what, double steady, double event, double reference) {
    const double ratio = event / std::max(steady, 1e-9);
    std::printf("  %-48s x%.1f (release reference x%.1f)\n", what, ratio, reference);
    assert(ratio <= std::max(12.0, reference * 1.5) && "audible discontinuity");
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("v32 live click-free regression @ %u Hz / %u\n", SR, BUF);

    // Reference: a normal organ key release.
    double releaseRef = 0;
    {
        auto e = engine(); pureOrgan(*e);
        for (int k = 0; k < 6; ++k) e->noteOn(36 + k * 4, 100);
        Capture c; c.render(*e, 40); const size_t mark = c.frames();
        for (int k = 0; k < 6; ++k) e->noteOff(36 + k * 4);
        c.render(*e, 4);
        releaseRef = c.curvature(mark - 2) / Capture{std::vector<float>(c.x.begin() + (mark - 400) * 2, c.x.begin() + mark * 2)}.curvature();
    }

    // Organ controls moved while a chord is held.
    struct Move { const char* name; void (*apply)(StageOrganModule&); };
    const Move moves[] = {
        {"drawbars 8' 8->0, 16' 0->8", [](StageOrganModule& o) { o.drawbars[2] = 0; o.drawbars[0] = 1; }},
        {"organ volume 0.8 -> 0.2", [](StageOrganModule& o) { o.organVolume = .2f; }},
        {"model B3 -> Combo", [](StageOrganModule& o) { o.organModel = 6; }},
        {"drive 0 -> 0.6", [](StageOrganModule& o) { o.organDrive = .6f; }},
        {"organ switched OFF", [](StageOrganModule& o) { o.organEnabled = false; }},
    };
    for (const auto& m : moves) {
        auto e = engine(); pureOrgan(*e);
        for (int k = 0; k < 6; ++k) e->noteOn(36 + k * 4, 100);
        Capture c; c.render(*e, 40); const size_t mark = c.frames();
        const double steady = c.curvature(mark - 400);
        m.apply(e->organModule_);
        c.render(*e, 6);
        const size_t eventEnd = c.frames();
        c.render(*e, 30);
        // Baseline: the larger of the steady signals before and after (drive adds
        // harmonics on purpose, so the settled driven tone is itself more curved).
        Capture settled{std::vector<float>(c.x.end() - 800, c.x.end())};
        Capture event{std::vector<float>(c.x.begin() + (mark - 2) * 2, c.x.begin() + eventEnd * 2)};
        expectSmooth(m.name, std::max(steady, settled.curvature()), event.curvature(), releaseRef);
    }
    {   // OFF: after the fade the organ is skipped entirely and its voices are free.
        auto e = engine(); pureOrgan(*e);
        e->noteOn(48, 100); Capture c; c.render(*e, 20);
        e->organModule_.organEnabled = false; c.render(*e, 20);
        assert(!e->organModule_.needsRender() && e->organModule_.activeVoices() == 0);
        std::printf("  organ OFF: silent, no voices, not rendered\n");
    }

    // Budget changes and stealing never cut organ voices.
    {
        auto e = engine(32); pureOrgan(*e);
        for (int k = 0; k < 12; ++k) e->noteOn(36 + k, 100);
        Capture c; c.render(*e, 40); const size_t mark = c.frames();
        const double steady = c.curvature(mark - 400);
        e->multiSynth_.synthEnabled = true; e->setPianoEnabled(true);
        c.render(*e, 6);
        expectSmooth("organ, piano + synth switched on (budget)", steady, c.curvature(mark - 2), releaseRef);
    }
    {
        auto e = engine(16); pureOrgan(*e);
        for (int k = 0; k < 16; ++k) e->noteOn(36 + k, 100);
        Capture c; c.render(*e, 40); const size_t mark = c.frames();
        const double steady = c.curvature(mark - 400);
        e->noteOn(60, 100); c.render(*e, 6);
        expectSmooth("organ voice steal (all voices held)", steady, c.curvature(mark - 2), 2 * releaseRef);
    }

    // Piano: all-notes-off fades; the pool keeps piano voices while organ + synth are on.
    {
        auto e = engine();
        e->setLayerInstrument(0, sineBank(1, 130.81)); e->setPianoLayerEnabled(0, true);
        for (int k = 0; k < 4; ++k) e->noteOn(48 + k * 5, 100);
        Capture c; c.render(*e, 40); const size_t mark = c.frames();
        const double steady = c.curvature(mark - 400);
        e->allNotesOff(); c.render(*e, 12);
        expectSmooth("piano all-notes-off", steady, c.curvature(mark - 2), releaseRef);
        std::printf("  piano after all-notes-off: peak %.2e, voices %d\n", c.peak(c.frames() - BUF), e->getActiveVoiceCount());
        assert(c.peak(c.frames() - BUF) < 1e-6 && "voices finished after the fade");
    }
    {
        auto e = engine(128);
        for (int l = 0; l < 3; ++l) { e->setLayerInstrument(l, sineBank(l + 1, 261.63)); e->setPianoLayerEnabled(l, true); }
        pureOrgan(*e); e->multiSynth_.synthEnabled = true;
        e->setSustainPedal(true);
        Capture c;
        for (int k = 0; k < 24; ++k) { e->noteOn(36 + k * 2, 100); c.render(*e, 3); }
        c.render(*e, 10);
        const int total = e->getActiveVoiceCount();
        const int organ = e->organModule_.activeVoices(), synth = e->multiSynth_.activeVoices();
        const int piano = total - organ - synth;
        std::printf("  3 piano layers + organ + synth, 24 pedalled notes: piano %d / 72 voices\n", piano);
        assert(piano >= 72 && "piano starved by idle modules");
    }

    // Piano insert echo: OFF crossfades, a re-enabled echo starts clean.
    {
        auto e = engine();
        e->setLayerInstrument(0, sineBank(1, 220.0)); e->setPianoLayerEnabled(0, true);
        InsertFxChain fx{}; fx[INSERT_ECHO].enabled = true; fx[INSERT_ECHO].mix = .5f; fx[INSERT_ECHO].feedback = .6f; fx[INSERT_ECHO].timeMs = 300.f;
        e->setPianoLayerInserts(0, fx);
        e->noteOn(57, 110);
        Capture c; c.render(*e, 60); const size_t mark = c.frames();
        const double steady = c.curvature(mark - 400);
        fx[INSERT_ECHO].enabled = false; e->setPianoLayerInserts(0, fx);
        c.render(*e, 6);
        expectSmooth("insert echo switched OFF while playing", steady, c.curvature(mark - 2), releaseRef);
        e->noteOff(57); e->allNotesOff();
        c.render(*e, 120);                                    // fades + incremental wipe
        fx[INSERT_ECHO].enabled = true; e->setPianoLayerInserts(0, fx);
        const size_t again = c.frames(); c.render(*e, 80);
        std::printf("  echo re-enabled with no notes: peak %.2e (stale echoes would be audible)\n", c.peak(again));
        assert(c.peak(again) < 1e-5 && "stale echo replayed");
    }

    // Studio slot: OFF / replaced effects ring out, then cost nothing.
    {
        auto e = engine();
        e->setLayerInstrument(0, sineBank(1, 220.0)); e->setPianoLayerEnabled(0, true);
        e->setStudioSlot(0, 0, 0, 6); e->setStudioSlot(0, 0, 1, 1);        // reverb ON
        e->setStudioSend(0, 0, 0, .8f);
        e->noteOn(57, 110);
        Capture c; c.render(*e, 30);
        e->noteOff(57); e->allNotesOff();
        c.render(*e, 8);                                        // dry gone, reverb tail ringing
        const size_t mark = c.frames();
        const double steady = c.curvature(mark - 400);
        const double tailBefore = c.peak(mark - BUF);
        e->setStudioSlot(0, 0, 1, 0);                           // slot OFF: spill-over
        c.render(*e, 4);
        expectSmooth("studio reverb switched OFF (tail rings out)", steady, c.curvature(mark - 2), releaseRef);
        std::printf("  reverb tail after OFF: %.2e -> %.2e (keeps ringing)\n", tailBefore, c.peak(mark + BUF));
        assert(c.peak(mark + BUF) > tailBefore * .2 && "tail cut when the slot was switched off");
        e->setStudioSlot(0, 0, 1, 1);                           // back ON: same processor
        e->setStudioSlot(0, 0, 0, 4);                           // replaced by a flanger
        const size_t swap = c.frames(); c.render(*e, 4);
        assert(c.peak(swap) > 0 && "replaced reverb must ring out");
        c.render(*e, int(25.0 * SR / BUF));                     // long after every tail
        std::printf("  replaced effect rang out; 25 s later the output is %.1e\n", c.peak(c.frames() - BUF));
        assert(c.peak(c.frames() - BUF) < 1e-7 && "idle rack must be silent (-140 dB)");
    }

    // Master limiter: stacked loud layers stay under the -0.3 dBFS ceiling.
    {
        auto e = engine();
        for (int l = 0; l < 5; ++l) { e->setLayerInstrument(l, sineBank(l + 1, 110.0 * (l + 1), .9f)); e->setPianoLayerEnabled(l, true); }
        e->setSustainPedal(true);
        Capture c; for (int k = 0; k < 12; ++k) e->noteOn(40 + k * 3, 127);
        c.render(*e, 60);
        std::printf("  5 loud layers x 12 notes: output peak %.3f, limiter %.1f dB\n", c.peak(), e->getLimiterReductionDb());
        assert(c.peak() <= 1.0 && e->getLimiterReductionDb() < 0.f);
    }

    std::printf("OK v32 live click-free regression\n");
    return 0;
}
