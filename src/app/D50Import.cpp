// D-50 patch -> Live Stage LA32 layers. See D50Import.h.
// Parameter offsets follow the D-50 MIDI implementation (partial 64 bytes, tone common
// 64 bytes, patch 64 bytes); every field was validated against the ranges of a real bank.
#include "app/D50Import.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace D50 {
using json = nlohmann::json;

namespace {
// Patch layout: upper partial 1, upper partial 2, upper common, lower partial 1,
// lower partial 2, lower common, patch block (64 bytes each).
constexpr int UP1 = 0x000, UP2 = 0x040, UCOM = 0x080, LP1 = 0x0C0, LP2 = 0x100, LCOM = 0x140, PATCH = 0x180;
constexpr int BANK_BASE = 0x8000;             // address 02 00 00 (7-bit) = first patch

const char kCharset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890-";

std::string decodeName(const Patch& p, int offset, int length) {
    std::string s;
    for (int i = 0; i < length; ++i) { const uint8_t c = p[size_t(offset + i)]; s += c < 64 ? kCharset[c] : '?'; }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// Approximate D-50 envelope time (0-100) in seconds.
float envTime(int value) {
    const float v = std::clamp(value, 0, 100) / 100.f;
    return 0.002f + 12.f * std::pow(v, 3.2f);
}

// Key follow tables (Munt, matching a real LAPC-I): sixteenths of 21.
const int kKeyfollow21[] = {-21, -10, -5, 0, 2, 5, 8, 10, 13, 16, 18, 21, 26, 32, 42, 21, 21};

// --- PCM waves: the D-50 wave ROM is not available, so each wave family maps to a
// procedural attack (one-shots 1-47) or to an LA32 sustain partial (loops 48-100).
enum Transient { BREATH = 0, MALLET = 1, PLUCK = 2, CHIFF = 3, CLICK = 4, GLASS = 5, BOW = 6, SPIT = 7 };
int oneShotTransient(int wave /*1-based*/) {
    static const int table[48] = { -1,
        MALLET, GLASS, MALLET, MALLET, MALLET, CLICK, MALLET, GLASS, PLUCK, PLUCK,      //  1-10
        GLASS, GLASS, GLASS, BOW, PLUCK, CLICK, CLICK, CLICK, PLUCK, PLUCK,             // 11-20
        CLICK, PLUCK, PLUCK, PLUCK, PLUCK, PLUCK, PLUCK, CLICK, CLICK, PLUCK,           // 21-30
        BREATH, BREATH, BREATH, CHIFF, CHIFF, SPIT, CHIFF, CHIFF, SPIT, SPIT,           // 31-40
        SPIT, SPIT, BOW, BOW, BOW, BOW, PLUCK };                                        // 41-47
    return wave >= 1 && wave <= 47 ? table[wave] : -1;
}
// Tone / patch names are a better hint than the wave number when they name an instrument.
int transientFromName(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    auto has = [&](std::initializer_list<const char*> words) {
        for (const char* w : words) if (name.find(w) != std::string::npos) return true;
        return false;
    };
    if (has({"breath", "breather", "blow", "voice", "choir", "singer", "chorale", "vox", "male", "female"})) return BREATH;
    if (has({"chiff", "flute", "shaku", "pipe", "calliope"})) return CHIFF;
    if (has({"marimba", "mallet", "xylo", "log", "kalimba"})) return MALLET;
    if (has({"bell", "vibe", "glass", "chime", "tine"})) return GLASS;
    if (has({"harp", "guitar", "gitar", "pick", "pik", "pizz", "nylon", "steel", "chink", "pluck", "koto"})) return PLUCK;
    if (has({"piano", "ep ", "epiano", "elecp", "hammer", "slap", "thump", "attack"})) return CLICK;
    if (has({"string", "strng", "cello", "violin", "bow", "arco", "resin"})) return BOW;
    if (has({"brass", "horn", "trump", "lips", "bone", "trombon"})) return SPIT;
    return -1;
}

struct PartialSource { int base; bool pcm; };

json insertFx() {
    json arr = json::array();
    for (int i = 0; i < 6; ++i)
        arr.push_back({{"enabled", false}, {"rateHz", .5}, {"depth", .5}, {"mix", .3}, {"feedback", .15}, {"timeMs", 280},
                       {"syncEnabled", false}, {"syncDivision", 5}, {"type", 1}, {"damping", .45}, {"preDelayMs", 12}});
    return arr;
}

// The D-50 tone chorus (type, rate, depth, balance) on the layer's insert chain.
void applyChorus(json& layer, const Patch& p, int common) {
    const int type = p[size_t(common + 42)], rate = p[size_t(common + 43)], depth = p[size_t(common + 44)], bal = p[size_t(common + 45)];
    const float mix = std::clamp(bal / 100.f, 0.f, 1.f) * .6f;
    if (mix < .02f) return;
    const float hz = std::clamp(.1f * std::pow(2.f, rate / 15.f), .05f, 7.f);
    // 0 Chorus 1, 1 Chorus 2, 2 Flanger 1, 3 Flanger 2, 4 FB Chorus, 5 Tremolo, 6 Chorus Tremolo, 7 Dimension
    const int unit = (type == 1 || type == 3 || type == 7) ? 1 : 0;
    auto& u = layer["insertFx"][size_t(unit)];
    u["enabled"] = true; u["rateHz"] = hz; u["mix"] = mix;
    u["depth"] = std::clamp(depth / 100.f, .05f, 1.f) * (type == 7 ? .5f : 1.f);
    u["feedback"] = (type == 2 || type == 3) ? .45 : type == 4 ? .3 : .05;
}

json makeLayer(const Patch& p, const PartialSource& src, const PartialSource* ringWith, int toneCommon,
               int keyShift, float fineCents, float gain, const std::string& toneName, int keyLow, int keyHigh) {
    const int b = src.base;
    auto at = [&](int o) { return int(p[size_t(b + o)]); };
    json L = {{"enabled", true}, {"synthesisType", 7}, {"filterType", 0}, {"insertFx", insertFx()},
              {"keyLow", keyLow}, {"keyHigh", keyHigh}, {"highpassHz", 20.0}, {"keyTrack", 1.0}};
    // ---- WG: pitch (coarse 36 = C4 = played key), fine, waveform, pulse width
    int semis = (at(0) - 36) + keyShift;
    if (src.pcm) {
        // PCM coarse values are tuned to the original sample's root: keep the interval,
        // fold the octaves (the substitute partial is pitched from the played key).
        while (semis > 12) semis -= 12;
        while (semis < -12) semis += 12;
    }
    const int octave = int(std::floor(semis / 12.0));
    L["octave"] = std::clamp(octave, -3, 3);
    L["semitones"] = semis - 12 * octave;
    L["detuneCents"] = std::clamp((at(1) - 50) + fineCents, -100.f, 100.f);
    const bool saw = at(6) == 1;
    const float pw = 0.5f - std::clamp(at(8), 0, 100) / 100.f * .45f;
    L["pulseWidth"] = pw;
    // ---- TVA: level, velocity, envelope
    const float tvaGain = std::pow(10.f, (at(35) - 100) * .4f / 20.f);
    L["velToAmp"] = std::clamp(at(36) / 100.f * 1.5f, 0.f, 2.f);
    const int t1 = at(39), t2 = at(40), t3 = at(41), t4 = at(42), t5 = at(43);
    const int peak = std::max({at(44), at(45), at(46), 1});
    L["attackSec"] = envTime(t1);
    L["decaySec"] = envTime(t2) + envTime(t3) + envTime(t4);
    L["sustainLevel"] = std::clamp(at(47) / float(peak), 0.f, 1.f);
    L["releaseSec"] = std::max(.02f, envTime(t5));
    L["volume"] = std::clamp(.75f * gain * tvaGain, 0.f, 1.5f);
    // ---- TVF (Munt's LA32 arithmetic): base cutoff = cutoff + 78 sixteenths of an
    // octave above the note at C4; relative key follow; envelope depth.
    const int kfDiff = kKeyfollow21[std::clamp(at(15), 0, 16)] - kKeyfollow21[std::clamp(at(2), 0, 16)];
    L["filterKeyTrack"] = std::clamp(1.0 + kfDiff * 12.0 / 256.0, -1.0, 2.0);
    L["cutoffHz"] = std::clamp(261.63 * std::pow(2.0, (at(13) + 78 - 128) / 16.0), 30.0, 18000.0);
    L["resonanceQ"] = 0.5f + std::clamp(at(14), 0, 30) / 30.f * 11.5f;
    const float envOctaves = (109.f * at(18) / 64.f) * (std::max({at(27), at(28), at(29), 1}) / 100.f) / 16.f;
    L["filterEnvAmt"] = std::clamp(envOctaves / 5.f, 0.f, 1.f);
    const int fpeak = std::max({at(27), at(28), at(29), 1});
    L["filtAttackSec"] = envTime(at(22));
    L["filtDecaySec"] = envTime(at(23)) + envTime(at(24)) + envTime(at(25));
    L["filtSustainLvl"] = std::clamp(at(30) / float(fpeak), 0.f, 1.f);
    L["filtReleaseSec"] = std::max(.02f, envTime(at(26)));
    L["velToCutoff"] = std::clamp(at(19) / 100.f * .6f, 0.f, 1.f);

    if (!src.pcm) {
        L["sawLevel"] = saw ? 1.0 : 0.0;
        L["pulseLevel"] = saw ? 0.0 : 1.0;
    } else {
        const int wave = at(7) + 1;
        int transient = transientFromName(toneName);
        const int byWave = oneShotTransient(wave);
        if (byWave >= 0 && transient < 0) transient = byWave;
        if (wave <= 47) {
            // One-shot PCM attack: procedural transient, no sustained oscillator.
            L["sawLevel"] = 0.0; L["pulseLevel"] = 0.0;
            L["transientType"] = transient < 0 ? CLICK : transient;
            L["transientLevel"] = std::clamp(.85f * tvaGain * gain, .1f, 1.f);
            L["transientDecay"] = std::clamp(envTime(t2) + envTime(t3), .03f, 1.5f);
            L["transientTone"] = std::clamp(.35f + at(13) / 200.f, .2f, .9f);
        } else {
            // Loop PCM: an LA32 sustain partial (organ / EP loops square, others sawtooth),
            // softened, plus a light attack when the name suggests one.
            const bool squareLoop = (wave >= 48 && wave <= 53) || (wave % 2 == 0);
            L["sawLevel"] = squareLoop ? 0.0 : .8;
            L["pulseLevel"] = squareLoop ? .8 : 0.0;
            L["cutoffHz"] = std::clamp(261.63 * std::pow(2.0, (70 + 78 - 128) / 16.0), 30.0, 18000.0);
            L["filterKeyTrack"] = 1.0;
            if (transient >= 0) { L["transientType"] = transient; L["transientLevel"] = .35; L["transientDecay"] = .15; }
        }
    }
    if (ringWith) {
        // Ring structure: partial 2 only ring-modulates partial 1 (P1 + P1 x P2 on the D-50).
        const int b2 = ringWith->base;
        L["osc2Level"] = std::clamp(std::pow(10.f, (p[size_t(b2 + 35)] - 100) * .4f / 20.f), 0.f, 1.f);
        const int ringSemis = (p[size_t(b2 + 0)] - 36) - (at(0) - 36);
        L["osc2Semitones"] = std::clamp(ringSemis, -48, 48);
        L["osc2Detune"] = std::clamp(int(p[size_t(b2 + 1)]) - 50, -50, 50);
        L["osc2Wave"] = p[size_t(b2 + 6)] == 1 ? 0 : 1;
        L["laRing"] = .7;
    }
    applyChorus(L, p, toneCommon);
    return L;
}
} // namespace

bool parseBank(const std::vector<uint8_t>& d, std::vector<Patch>& patches, std::string& error) {
    std::map<int, uint8_t> mem;
    for (size_t i = 0; i < d.size();) {
        if (d[i] != 0xF0) { ++i; continue; }
        size_t j = i + 1;
        while (j < d.size() && d[j] != 0xF7) ++j;
        if (j >= d.size()) break;
        // F0 41 dev 14 12 a1 a2 a3 data... checksum F7  (Roland DT1, model D-50)
        if (j - i > 10 && d[i + 1] == 0x41 && d[i + 3] == 0x14 && d[i + 4] == 0x12) {
            const int addr = (d[i + 5] << 14) | (d[i + 6] << 7) | d[i + 7];
            for (size_t k = i + 8; k + 1 < j; ++k) mem[addr + int(k - (i + 8))] = d[k] & 0x7f;
        }
        i = j + 1;
    }
    patches.clear();
    for (int n = 0; n < 64; ++n) {
        Patch p{};
        const int base = BANK_BASE + n * PATCH_BYTES;
        for (int k = 0; k < PATCH_BYTES; ++k) {
            const auto it = mem.find(base + k);
            if (it == mem.end()) {
                if (patches.empty()) { error = "No es un volcado de patches del D-50 (mensajes DT1 de Roland, modelo 14h)"; return false; }
                return true;                                     // partial bank: keep what is complete
            }
            p[size_t(k)] = it->second;
        }
        patches.push_back(p);
    }
    return true;
}

std::string patchName(const Patch& p) { return decodeName(p, PATCH, 18); }
std::string upperToneName(const Patch& p) { return decodeName(p, UCOM, 10); }
std::string lowerToneName(const Patch& p) { return decodeName(p, LCOM, 10); }
std::string keyModeName(const Patch& p) {
    static const char* modes[] = {"WHOLE", "DUAL", "SPLIT", "SEPARATE", "WHOLE-S", "DUAL-S", "SPLIT-US", "SPLIT-LS", "SEPARATE-S"};
    return modes[std::min<int>(p[PATCH + 18], 8)];
}

json toSynthParams(const Patch& p) {
    const int keyMode = std::min<int>(p[PATCH + 18], 8);
    const int splitKey = 36 + std::clamp<int>(p[PATCH + 19], 0, 60);         // split point C2-C7
    const bool useLower = keyMode != 0 && keyMode != 4;
    const bool split = keyMode == 2 || keyMode == 6 || keyMode == 7;
    const bool solo = keyMode >= 4;
    const float toneBalance = p[PATCH + 33] / 100.f;                          // 0 = lower, 100 = upper
    const float total = std::clamp(p[PATCH + 32] / 100.f, .2f, 1.f);

    json layers = json::array();
    auto addTone = [&](int common, int part1, int part2, int keyShift, float fine, float gain, int lo, int hi) {
        const int structure = std::min<int>(p[size_t(common + 10)], 6);
        // 1 S+S, 2 S+(SxS), 3 P+S, 4 P+(PxS), 5 S+(SxP), 6 P+P, 7 P+(PxP)
        const bool p1pcm = structure == 2 || structure == 3 || structure == 5 || structure == 6;
        const bool p2pcm = structure == 4 || structure == 5 || structure == 6;
        const bool ring = structure == 1 || structure == 3 || structure == 4 || structure == 6;
        const int mute = p[size_t(common + 46)];                              // bit0 = P1 on, bit1 = P2 on
        const float bal = p[size_t(common + 47)] / 100.f;                     // 0 = P1, 100 = P2
        const float g1 = gain * std::min(1.f, (1.f - bal) * 2.f), g2 = gain * std::min(1.f, bal * 2.f);
        const std::string name = common == UCOM ? upperToneName(p) : lowerToneName(p);
        const PartialSource s1{part1, p1pcm}, s2{part2, p2pcm};
        const bool on1 = (mute & 1) != 0, on2 = (mute & 2) != 0;
        if (ring && !p1pcm && !p2pcm && on1 && on2) {
            layers.push_back(makeLayer(p, s1, &s2, common, keyShift, fine, g1, name, lo, hi));
            return;
        }
        if (on1) layers.push_back(makeLayer(p, s1, nullptr, common, keyShift, fine, g1, name, lo, hi));
        if (on2) layers.push_back(makeLayer(p, s2, nullptr, common, keyShift, fine, g2, name, lo, hi));
    };
    const float upperGain = useLower ? std::min(1.f, toneBalance * 2.f) : 1.f;
    const float lowerGain = std::min(1.f, (1.f - toneBalance) * 2.f);
    addTone(UCOM, UP1, UP2, int(p[PATCH + 22]) - 24, (p[PATCH + 24] - 50) * 1.f, upperGain * total,
            split ? splitKey : 0, 127);
    if (useLower)
        addTone(LCOM, LP1, LP2, int(p[PATCH + 23]) - 24, (p[PATCH + 25] - 50) * 1.f, lowerGain * total,
                0, split ? splitKey - 1 : 127);
    while (layers.size() > 4) layers.erase(layers.end() - 1);           // the synth has 4 layers
    while (layers.size() < 4) layers.push_back({{"enabled", false}, {"synthesisType", 7}, {"insertFx", insertFx()}});

    // Patch reverb (types 1-32 on the D-50: halls, rooms, plates, delays, gates).
    const int rev = p[PATCH + 30], revBal = p[PATCH + 31];
    const int reverbType = rev < 8 ? 2 : rev < 16 ? 0 : 1;                     // HALL, ROOM, PLATE
    json fx = {{"chorusEnabled", false}, {"delayEnabled", false}, {"driveEnabled", false}, {"autoPanEnabled", false},
               {"phaserEnabled", false}, {"tremoloEnabled", false}, {"flangerEnabled", false},
               {"reverbEnabled", revBal > 2}, {"reverbType", reverbType}, {"reverbMix", std::clamp(revBal / 100.f * .55f, 0.f, .6f)},
               {"reverbSize", rev < 8 ? .75 : rev < 16 ? .45 : .6}};
    json params = {{"synthEnabled", true}, {"synthMasterVol", .82}, {"synthMacroCutoff", 1.0},
                   {"synthWheelGlobalCutoff", 0.0}, {"synthWheelGlobalAmpDb", 0.0}, {"layers", layers},
                   {"synthVoiceMaster", true}, {"synthVoiceMode", solo ? 1 : 0},
                   {"synthPortamentoEnabled", solo && p[PATCH + 28] > 0}, {"synthPortamento", p[PATCH + 28] / 100.f * 1.5f},
                   {"synthGlideMode", 1}, {"synthSlide", 0.0}, {"synthFx", fx}, {"synthFxSends", json::array()}};
    return params;
}

} // namespace D50
