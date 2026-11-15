#include "ParamValueReader.h"
#include <algorithm>
#include <mutex>
#include <regex>
#include <unordered_map>
#include <variant>
#include <vector>

namespace NordDSP {
namespace {
using json = nlohmann::json;
using Key = std::variant<std::string, int>;
using Path = std::vector<Key>;
using Table = std::unordered_map<std::string, std::string>;

// --- tables: keep in step with webui/src/params/paramMap.ts --------------------
const Table DIRECT = {
    {"tune", "masterTuneCents"}, {"cutoff", "cutoffHz"}, {"resonance", "resonanceQ"},
    {"attack", "attackSec"}, {"decay", "decaySec"}, {"sustain", "sustainLevel"}, {"release", "releaseSec"},
    {"lowCut", "lowCutHz"}, {"highCut", "highCutHz"},
    {"chorusRate", "chorusRateHz"}, {"phaserRate", "phaserRateHz"}, {"tremoloRate", "tremoloRateHz"},
    {"transpose", "transposeSemis"}, {"octave", "octaveShift"}, {"touch", "kbdTouch"},
    {"masterDriveOn", "masterDriveEnabled"}, {"masterAutoPanOn", "masterAutoPanEnabled"},
    {"synthPortamentoOn", "synthPortamentoEnabled"},
    {"synthSeqClock", "synthSeqGlobalClock"}, {"synthSeqShare", "synthSeqSharePattern"},
};
const Table SAMPLE_FIELDS = {
    {"Atk", "attackSec"}, {"Dec", "decaySec"}, {"Sus", "sustainLevel"}, {"Rel", "releaseSec"}, {"Hold", "loopHold"},
    {"Pan", "pan"}, {"Tune", "fineTuneCents"}, {"Cut", "cutoffHz"}, {"Res", "resonanceQ"}, {"Gain", "gainDb"},
    {"WheelCutoff", "wheelCutoffOctaves"}, {"WheelAmp", "wheelAmpDb"}, {"PortOn", "portamentoEnabled"},
    {"PortTime", "portamentoSec"}, {"PortMode", "portamentoMode"}, {"Mono", "monoMode"}, {"Legato", "legatoEnabled"},
    {"WheelCutoffOn", "wheelCutoffEnabled"}, {"WheelCutoffInvert", "wheelCutoffInvert"}, {"WheelAmpOn", "wheelAmpEnabled"},
    {"WheelVibratoOn", "wheelVibratoEnabled"}, {"WheelVibratoDepth", "wheelVibratoCents"}, {"WheelVibratoRate", "wheelVibratoRateHz"},
    {"EqHP", "soundEqHighPass"}, {"EqLP", "soundEqLowPass"},
    {"Eq0", "soundEqEnabled"}, {"Eq1", "soundEqHighPass"}, {"Eq2", "soundEqLowPass"}, {"Eq3", "soundEqHighPassSlope"}, {"Eq4", "soundEqLowPassSlope"},
};
const Table SAMPLE_BAND = {{"EqF", "soundEqFreq"}, {"EqQ", "soundEqQ"}, {"EqG", "soundEqGain"}};
const Table LAYER_FIELDS = {
    {"en", "enabled"}, {"wave", "waveType"}, {"vol", "volume"}, {"pan", "pan"}, {"oct", "octave"}, {"semi", "semitones"}, {"det", "detuneCents"},
    {"cut", "cutoffHz"}, {"res", "resonanceQ"}, {"atk", "attackSec"}, {"dec", "decaySec"}, {"sus", "sustainLevel"}, {"rel", "releaseSec"},
    {"unison", "unisonVoices"}, {"uniDet", "unisonDetune"}, {"uniSprd", "unisonSpread"}, {"phase", "phaseOffset"}, {"keyTrk", "keyTrack"},
    {"velCut", "velToCutoff"}, {"velAmp", "velToAmp"}, {"wtIdx", "wavetableIdx"}, {"wtMorph", "wavetableMorph"},
    {"filtType", "filterType"}, {"filtEnv", "filterEnvAmt"}, {"filtKT", "filterKeyTrack"},
    {"fAtk", "filtAttackSec"}, {"fDec", "filtDecaySec"}, {"fSus", "filtSustainLvl"}, {"fRel", "filtReleaseSec"},
    {"saw", "sawLevel"}, {"pulse", "pulseLevel"}, {"pw", "pulseWidth"}, {"pwmDepth", "pwmDepth"}, {"pwmRate", "pwmRateHz"},
    {"sub", "subLevel"}, {"noise", "noiseLevel"}, {"osc2", "osc2Level"}, {"osc2Det", "osc2Detune"}, {"osc2Semi", "osc2Semitones"},
    {"osc2Wave", "osc2Wave"}, {"hpf", "highpassHz"}, {"drive", "drive"}, {"synthType", "synthesisType"}, {"fmIndex", "fmIndex"},
    {"fmRatio", "fmRatio"}, {"wheelMode", "wheelMode"}, {"wheelCutoff", "wheelCutoffOctaves"}, {"wheelAmp", "wheelAmpDb"},
    {"wheelVibrato", "wheelVibratoCents"}, {"wheelVibRate", "wheelVibratoRateHz"}, {"wheelWtMorph", "wheelWtMorph"},
    {"voiceMode", "voiceMode"}, {"portamentoOn", "portamentoEnabled"}, {"portamento", "portamento"}, {"glideMode", "glideMode"},
    {"slide", "slide"}, {"keyLow", "keyLow"}, {"keyHigh", "keyHigh"}, {"keyTrans", "keyTranspose"},
    {"seqOn", "seqEnabled"}, {"seqBpm", "seqBpm"}, {"seqSteps", "seqSteps"}, {"seqGate", "seqGate"}, {"seqDiv", "seqDivision"}, {"seqSync", "seqSync"},
    {"analog", "analog"}, {"fm4Algo", "fm4Algorithm"}, {"fm4Fb", "fm4Feedback"}, {"fm4Vel", "fm4VelMod"}, {"laMix", "laMix"}, {"laRing", "laRing"},
    {"trType", "transientType"}, {"trLevel", "transientLevel"}, {"trDecay", "transientDecay"}, {"trBright", "transientTone"},
};
const Table OP_FIELDS = {{"Ratio", "opRatio"}, {"Level", "opLevel"}, {"Atk", "opAttack"}, {"Dec", "opDecay"}, {"Sus", "opSustain"}};
const Table LFO_FIELDS = {
    {"lfoEnabled", "enabled"}, {"lfoWave", "waveType"}, {"lfoRate", "rateHz"}, {"lfoDepth", "depth"}, {"lfoPitch", "destPitch"},
    {"lfoCutoff", "destCutoff"}, {"lfoVolume", "destVolume"}, {"lfoPan", "destPan"},
    {"lfoDelay", "delaySec"}, {"lfoFade", "fadeSec"}, {"lfoSync", "keySync"},
};
const Table INSERT_FIELDS = {
    {"on", "enabled"}, {"rate", "rateHz"}, {"depth", "depth"}, {"mix", "mix"}, {"feedback", "feedback"},
    {"time", "timeMs"}, {"sync", "syncEnabled"}, {"division", "syncDivision"},
    {"type", "type"}, {"damp", "damping"}, {"predelay", "preDelayMs"},
};
const Table BUS = {{"Mod", "modulation"}, {"Delay", "delay"}, {"Reverb", "reverb"}};
const Table ZONE_EDGE = {{"Low", "low"}, {"High", "high"}};
const Table MODULE_FX = {
    {"ChorusOn", "chorusEnabled"}, {"ChorusRate", "chorusRate"}, {"ChorusDepth", "chorusDepth"}, {"ChorusMix", "chorusMix"},
    {"DelayOn", "delayEnabled"}, {"DelayTime", "delayMs"}, {"DelayFeedback", "delayFeedback"}, {"DelayMix", "delayMix"},
    {"ReverbOn", "reverbEnabled"}, {"ReverbType", "reverbType"}, {"ReverbMix", "reverbMix"}, {"ReverbSize", "reverbSize"},
    {"DriveOn", "driveEnabled"}, {"DriveAmount", "driveAmount"}, {"AutoPanOn", "autoPanEnabled"}, {"AutoPanRate", "autoPanRate"},
    {"AutoPanDepth", "autoPanDepth"}, {"PhaserOn", "phaserEnabled"}, {"PhaserRate", "phaserRate"}, {"PhaserDepth", "phaserDepth"},
    {"PhaserFeedback", "phaserFeedback"}, {"PhaserMix", "phaserMix"}, {"TremoloOn", "tremoloEnabled"}, {"TremoloRate", "tremoloRate"},
    {"TremoloDepth", "tremoloDepth"}, {"FlangerOn", "flangerEnabled"}, {"FlangerRate", "flangerRate"}, {"FlangerDepth", "flangerDepth"},
    {"FlangerFeedback", "flangerFeedback"}, {"FlangerMix", "flangerMix"},
};
const Table SYNTH_MACROS = {
    {"synth-unison-voices", "unisonVoices"}, {"synth-unison-detune", "unisonDetune"}, {"synth-unison-spread", "unisonSpread"},
    {"synth-phase", "phaseOffset"}, {"synth-keytrack", "keyTrack"}, {"synth-vel-cutoff", "velToCutoff"}, {"synth-vel-amp", "velToAmp"},
    {"synth-filt-envamt", "filterEnvAmt"}, {"synth-filt-atk", "filtAttackSec"}, {"synth-filt-dec", "filtDecaySec"},
    {"synth-filt-sus", "filtSustainLvl"}, {"synth-filt-rel", "filtReleaseSec"},
};

// A path is invalid (empty optional) when a lookup table has no entry for the id.
std::optional<std::string> look(const Table& t, const std::string& k) {
    const auto it = t.find(k);
    if (it == t.end()) return std::nullopt;
    return it->second;
}
int n(const std::smatch& m, size_t i, int offset = 0) { return std::stoi(m[i].str()) + offset; }

using Rule = std::pair<std::regex, std::optional<Path> (*)(const std::smatch&)>;
#define FIELD(table, key) ([&]() -> std::optional<std::string> { return look(table, key); })()

std::optional<Path> withField(Path prefix, std::optional<std::string> field, std::optional<int> tail = std::nullopt) {
    if (!field) return std::nullopt;
    prefix.emplace_back(*field);
    if (tail) prefix.emplace_back(*tail);
    return prefix;
}

const std::vector<Rule>& rules() {
    static const std::vector<Rule> list = {
        {std::regex(R"(^synth-[\w-]+$)"), [](const std::smatch& m) { return withField({std::string("synthLayers"), 0}, look(SYNTH_MACROS, m[0].str())); }},
        {std::regex(R"(^L([1-4])_fx([0-5])_(\w+)$)"), [](const std::smatch& m) { return withField({std::string("synthLayers"), n(m, 1, -1), std::string("insertFx"), n(m, 2)}, look(INSERT_FIELDS, m[3].str())); }},
        {std::regex(R"(^L([1-4])_seqStep(\d+)$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("synthLayers"), n(m, 1, -1), std::string("seqNotes"), n(m, 2)}); }},
        {std::regex(R"(^synthMacro([1-4])$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("synthMacros"), n(m, 1, -1)}); }},
        {std::regex(R"(^L([1-4])_seqAcc(\d+)$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("synthLayers"), n(m, 1, -1), std::string("seqAccent"), n(m, 2)}); }},
        {std::regex(R"(^L([1-4])_seqSlide(\d+)$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("synthLayers"), n(m, 1, -1), std::string("seqSlide"), n(m, 2)}); }},
        {std::regex(R"(^L([1-4])_op([1-4])(Ratio|Level|Atk|Dec|Sus)$)"), [](const std::smatch& m) { return withField({std::string("synthLayers"), n(m, 1, -1)}, look(OP_FIELDS, m[3].str()), n(m, 2, -1)); }},
        {std::regex(R"(^L([1-4])_(lfo\w+)$)"), [](const std::smatch& m) { return withField({std::string("synthLfos"), n(m, 1, -1)}, look(LFO_FIELDS, m[2].str())); }},
        {std::regex(R"(^L([1-4])_(\w+)$)"), [](const std::smatch& m) { return withField({std::string("synthLayers"), n(m, 1, -1)}, look(LAYER_FIELDS, m[2].str())); }},
        {std::regex(R"(^pianoFx([0-4])_([0-5])_(\w+)$)"), [](const std::smatch& m) { return withField({std::string("pianoSampleConfig"), n(m, 1), std::string("insertFx"), n(m, 2)}, look(INSERT_FIELDS, m[3].str())); }},
        {std::regex(R"(^pianoSample([0-4])(EqF|EqQ|EqG)([0-2])$)"), [](const std::smatch& m) { return withField({std::string("pianoSampleConfig"), n(m, 1)}, look(SAMPLE_BAND, m[2].str()), n(m, 3)); }},
        {std::regex(R"(^pianoSample([0-4])(\w+)$)"), [](const std::smatch& m) { return withField({std::string("pianoSampleConfig"), n(m, 1)}, look(SAMPLE_FIELDS, m[2].str())); }},
        {std::regex(R"(^pianoLayerVol([0-4])$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("pianoLayers"), n(m, 1), std::string("volume")}); }},
        {std::regex(R"(^pianoLayerEnabled([0-4])$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("pianoLayers"), n(m, 1), std::string("enabled")}); }},
        {std::regex(R"(^pianoZone([0-4])(Low|High)$)"), [](const std::smatch& m) { return withField({std::string("pianoLayerZones"), n(m, 1)}, look(ZONE_EDGE, m[2].str())); }},
        {std::regex(R"(^(piano|synth|organ)Zone(Low|High)$)"), [](const std::smatch& m) { return withField({m[1].str() + "Zone"}, look(ZONE_EDGE, m[2].str())); }},
        {std::regex(R"(^eqGain([0-4])$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("eqBands"), n(m, 1), std::string("gainDb")}); }},
        {std::regex(R"(^eqFreq([0-4])$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("eqBands"), n(m, 1), std::string("freqHz")}); }},
        {std::regex(R"(^eqQ([0-4])$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("eqBands"), n(m, 1), std::string("q")}); }},
        {std::regex(R"(^drawbar([0-8])$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("drawbars"), n(m, 1)}); }},
        {std::regex(R"(^fxSend(\d)(Mod|Delay|Reverb)$)"), [](const std::smatch& m) { return withField({std::string("fxSends"), n(m, 1)}, look(BUS, m[2].str())); }},
        {std::regex(R"(^synthSend(\d)(Mod|Delay|Reverb)$)"), [](const std::smatch& m) { return withField({std::string("synthFxSends"), n(m, 1)}, look(BUS, m[2].str())); }},
        {std::regex(R"(^organSend(Mod|Delay|Reverb)$)"), [](const std::smatch& m) { return withField({std::string("organFxSends")}, look(BUS, m[1].str())); }},
        {std::regex(R"(^(synth|organ)Fx(\w+)$)"), [](const std::smatch& m) { return withField({m[1].str() + "Fx"}, look(MODULE_FX, m[2].str())); }},
        {std::regex(R"(^studioSlot(\d)_(\d)_p(\d+)$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("studioRacks"), n(m, 1), std::string("slots"), n(m, 2), std::string("params"), n(m, 3)}); }},
        {std::regex(R"(^studioSend(\d)_(\d)_(\d)$)"), [](const std::smatch& m) { return std::optional<Path>(Path{std::string("studioRacks"), n(m, 1), std::string("sends"), n(m, 2), n(m, 3)}); }},
    };
    return list;
}

std::optional<Path> resolve(const std::string& id) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::optional<Path>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    if (auto it = cache.find(id); it != cache.end()) return it->second;
    std::optional<Path> path;
    if (auto d = DIRECT.find(id); d != DIRECT.end()) path = Path{d->second};
    else {
        bool matched = false;
        for (const auto& [re, read] : rules()) {
            std::smatch m;
            if (std::regex_match(id, m, re)) { path = read(m); matched = true; break; }
        }
        if (!matched) path = Path{id};
    }
    cache.emplace(id, path);
    return path;
}
}  // namespace

std::optional<float> readParamValue(const json& params, const std::string& id) {
    const auto path = resolve(id);
    if (!path) return std::nullopt;
    const json* cur = &params;
    for (const auto& key : *path) {
        if (const auto* s = std::get_if<std::string>(&key)) {
            if (!cur->is_object()) return std::nullopt;
            const auto it = cur->find(*s);
            if (it == cur->end()) return std::nullopt;
            cur = &*it;
        } else {
            const int i = std::get<int>(key);
            if (!cur->is_array() || i < 0 || size_t(i) >= cur->size()) return std::nullopt;
            cur = &(*cur)[size_t(i)];
        }
    }
    if (cur->is_boolean()) return cur->get<bool>() ? 1.f : 0.f;
    if (cur->is_number()) return cur->get<float>();
    return std::nullopt;
}

int parameterModule(const std::string& id) {
    auto starts = [&id](const char* p) { return id.rfind(p, 0) == 0; };
    static const std::regex synthLayer(R"(^L[1-4]_)");
    static const std::regex studio(R"(^studio(Slot|Send)(\d)_)");
    static const std::regex fxSend(R"(^fxSend(\d)(Mod|Delay|Reverb)$)");
    std::smatch m;
    if (std::regex_search(id, m, studio)) { const int rack = std::stoi(m[2].str()); return rack <= 2 ? rack : -1; }
    if (std::regex_match(id, m, fxSend)) {
        const int src = std::stoi(m[1].str());
        return src < 5 ? 0 : src < 9 ? 1 : 2;
    }
    if (std::regex_search(id, synthLayer) || starts("synth")) return 1;
    if (starts("organ") || starts("drawbar")) return 2;
    if (starts("piano")) return 0;
    return -1;
}

namespace {
// Sound-owned parameter families; group 1 is the layer digit.
const std::vector<std::regex>& soundPatterns() {
    static const std::vector<std::regex> list = {
        std::regex(R"(^pianoSample([0-4])\w+$)"),
        std::regex(R"(^pianoFx([0-4])_\d_\w+$)"),
        std::regex(R"(^fxSend([0-4])(Mod|Delay|Reverb)$)"),
        std::regex(R"(^studioSend0_([0-4])_\d+$)"),
    };
    return list;
}
}

int soundLayerOfParam(const std::string& id) {
    std::smatch m;
    for (const auto& re : soundPatterns())
        if (std::regex_match(id, m, re)) return std::stoi(m[1].str());
    return -1;
}

std::string retargetSoundParam(const std::string& id, int layer) {
    std::smatch m;
    for (const auto& re : soundPatterns())
        if (std::regex_match(id, m, re)) {
            std::string out = id;
            out.replace(size_t(m.position(1)), size_t(m.length(1)), std::to_string(std::clamp(layer, 0, 4)));
            return out;
        }
    return id;
}

} // namespace NordDSP
