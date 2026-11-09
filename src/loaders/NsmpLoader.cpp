#include "NsmpLoader.h"
#include "BankLoader.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <map>
#include <chrono>

namespace fs = std::filesystem;

namespace NordDSP {

namespace {

static std::atomic<uint64_t> g_nsmpInstId{10000};

inline uint16_t readU16BE_N(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

inline uint32_t readU24BE_N(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8)  |
            static_cast<uint32_t>(p[2]);
}

inline uint32_t readU32BE_N(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

const int NW1_PRED_COEFF[8][7] = {
    {0,   0,   0,   0,  0,  0, 0},
    {1,   0,   0,   0,  0,  0, 0},
    {2,  -1,   0,   0,  0,  0, 0},
    {3,  -3,   1,   0,  0,  0, 0},
    {4,  -6,   4,  -1,  0,  0, 0},
    {5, -10,  10,  -5,  1,  0, 0},
    {6, -15,  20, -15,  6, -1, 0},
    {7, -21,  35, -35, 21, -7, 1}
};

struct NsmpSectionInfo {
    std::string tag;
    uint32_t version = 0;
    uint32_t size = 0;
    size_t payloadOffset = 0;
    size_t endOffset = 0;
};

struct NsmpMark {
    uint32_t word = 0;
    uint32_t sample = 0;
    bool loopDecay = false;
};

struct DecodedNw1Stroke {
    std::vector<int32_t> chL;
    std::vector<int32_t> chR;
    int channelCount = 1;
    size_t endOffset = 0;
    std::vector<NsmpMark> marks;
};

inline int32_t signExtendNw1(uint32_t v, int bw) {
    uint32_t half = 1u << (bw - 1);
    return (v >= half) ? static_cast<int32_t>(v) - static_cast<int32_t>(1u << bw) : static_cast<int32_t>(v);
}

std::string extractAsciiRun(const std::vector<uint8_t>& bytes, size_t from, size_t to, size_t minLen = 2) {
    std::string run;
    size_t limit = std::min(to, bytes.size());
    for (size_t i = from; i < limit; ++i) {
        uint8_t c = bytes[i];
        if (c >= 0x20 && c < 0x7f) run.push_back(static_cast<char>(c));
        else if (run.size() >= minLen) break;
        else run.clear();
    }
    while (!run.empty() && (run.back() == ' ' || run.back() == '\t')) run.pop_back();
    while (!run.empty() && (run.front() == ' ' || run.front() == '\t')) run.erase(run.begin());
    return (run.size() >= minLen) ? run : "";
}

bool decodeNw1Stream(const std::vector<uint8_t>& bytes, size_t byteOffset, size_t sectionEnd,
                     int channelCount, bool wordInterleaved, bool u24, DecodedNw1Stroke& out) {
    const size_t hdrBytes  = u24 ? 3 : 4;
    const size_t wordBytes = u24 ? 3 : 4;
    const int wordBits     = u24 ? 24 : 32;

    struct ChState {
        int32_t ring[32] = {0};
        int head = 0;
        std::vector<int32_t> samples;
    };
    ChState chs[2];
    chs[0].samples.reserve(65536);
    if (channelCount > 1) chs[1].samples.reserve(65536);

    auto emitSample = [&](int c, int32_t residual, int order) -> bool {
        ChState& cs = chs[c];
        int64_t pred = 0;
        const int* coeff = NW1_PRED_COEFF[order];
        for (int k = 0; k < order; ++k) {
            pred += static_cast<int64_t>(coeff[k]) * cs.ring[(cs.head - 1 - k) & 31];
        }
        int64_t val = static_cast<int64_t>(residual) + pred;
        if (std::abs(val) >= (1LL << 26)) return false;
        cs.ring[cs.head] = static_cast<int32_t>(val);
        cs.head = (cs.head + 1) & 31;
        cs.samples.push_back(static_cast<int32_t>(val));
        return true;
    };

    out.marks.clear();
    size_t o = byteOffset;
    uint32_t index = 0;
    uint32_t cumIL = 0;

    while (o + hdrBytes <= sectionEnd) {
        uint32_t word = u24 ? readU24BE_N(&bytes[o]) : readU32BE_N(&bytes[o]);
        uint32_t sampleCnt = word & 0x3fff;
        int filterOrder    = (word >> 14) & 0xf;
        bool loopDecay     = ((word >> 18) & 1) != 0;
        int bitWidth       = ((word >> 19) & 0xf) + 1;
        bool linMode       = ((word >> 23) & 1) != 0;
        bool isStop        = (filterOrder == 0 && bitWidth == 1);

        if (linMode) {
            out.marks.push_back({
                static_cast<uint32_t>((o - byteOffset) / hdrBytes),
                wordInterleaved ? cumIL : index,
                loopDecay
            });
        }
        o += hdrBytes;

        if (wordInterleaved) {
            if (sampleCnt == 0) continue;
            if (filterOrder > 7) break;
            cumIL += sampleCnt;
            uint32_t perCh = sampleCnt / channelCount;
            uint32_t wordsPerCh = (perCh * bitWidth + 31) / 32;
            if (o + static_cast<size_t>(wordsPerCh) * channelCount * 4 > sectionEnd) break;

            uint64_t mask = (bitWidth == 32) ? 0xFFFFFFFFULL : ((1ULL << bitWidth) - 1ULL);
            for (int ch = 0; ch < channelCount; ++ch) {
                uint32_t count = (ch == channelCount - 1) ? (sampleCnt - perCh * (channelCount - 1)) : perCh;
                uint64_t acc = 0;
                int nbits = 0;
                uint32_t produced = 0;
                for (uint32_t w = 0; w < wordsPerCh; ++w) {
                    uint32_t wVal = readU32BE_N(&bytes[o + (w * channelCount + ch) * 4]);
                    acc |= (static_cast<uint64_t>(wVal) << (32 - nbits));
                    nbits += 32;
                    while (nbits >= bitWidth && produced < count) {
                        uint32_t rawBits = static_cast<uint32_t>((acc >> (64 - bitWidth)) & mask);
                        if (!emitSample(ch, signExtendNw1(rawBits, bitWidth), filterOrder)) return false;
                        produced++;
                        acc <<= bitWidth;
                        nbits -= bitWidth;
                    }
                }
            }
            o += static_cast<size_t>(wordsPerCh) * channelCount * 4;
            continue;
        }

        if (isStop) break;
        if (filterOrder > 7 || sampleCnt == 0) return false;

        uint32_t nWords = (sampleCnt * bitWidth + wordBits - 1) / wordBits;
        if (o + static_cast<size_t>(nWords) * wordBytes > sectionEnd) return false;

        uint64_t mask = (bitWidth == 32) ? 0xFFFFFFFFULL : ((1ULL << bitWidth) - 1ULL);
        uint64_t acc = 0;
        int nbits = 0;
        uint32_t produced = 0;
        for (uint32_t w = 0; w < nWords; ++w) {
            uint32_t wVal = u24 ? (readU24BE_N(&bytes[o]) << 8) : readU32BE_N(&bytes[o]);
            o += wordBytes;
            acc |= (static_cast<uint64_t>(wVal) << (32 - nbits));
            nbits += wordBits;
            while (nbits >= bitWidth && produced < sampleCnt) {
                uint32_t rawBits = static_cast<uint32_t>((acc >> (64 - bitWidth)) & mask);
                if (!emitSample(index % channelCount, signExtendNw1(rawBits, bitWidth), filterOrder)) return false;
                produced++;
                index++;
                acc <<= bitWidth;
                nbits -= bitWidth;
            }
        }
    }

    if (chs[0].samples.size() < 100) return false;
    out.channelCount = channelCount;
    out.endOffset = o;
    out.chL = std::move(chs[0].samples);
    out.chR = (channelCount > 1) ? std::move(chs[1].samples) : out.chL;
    return true;
}

} // namespace

bool NsmpLoader::loadNsmpNative(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg) {
    auto t0 = std::chrono::steady_clock::now();
    std::ifstream f(filePath, std::ios::binary | std::ios::ate);
    if (!f) return false;

    std::streamsize fileSize = f.tellg();
    if (fileSize < 128) return false;
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    if (!f.read(reinterpret_cast<char*>(bytes.data()), fileSize)) return false;

    size_t bodyStart = 0x2c;
    size_t headerSize = 12;
    bool legacy = false;

    auto matchTag = [&](size_t off, const char* t) {
        return off + 4 <= bytes.size() &&
               bytes[off] == t[0] && bytes[off + 1] == t[1] && bytes[off + 2] == t[2] && bytes[off + 3] == t[3];
    };

    if (matchTag(0x18, "NSMP")) {
        bodyStart = 0x18; headerSize = 12;
    } else if (matchTag(0x2c, "NSMP")) {
        bodyStart = 0x2c; headerSize = 12;
    } else if (0x18 + 3 <= bytes.size() &&
               ((bytes[0x18] == 'N' && bytes[0x19] == 'W' && bytes[0x1a] == 'S') ||
                (bytes[0x19] == 'N' && bytes[0x1a] == 'W' && bytes[0x1b] == 'S'))) {
        bodyStart = 0x18; headerSize = 9; legacy = true;
    } else {
        return false;
    }

    uint16_t versionRaw = bytes[0x14] | (static_cast<uint16_t>(bytes[0x15]) << 8);
    int codec = legacy ? (versionRaw == 200 ? 2 : 1) : (versionRaw / 100);
    std::string ext = fs::path(filePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".nsmp4") codec = 4;
    else if (ext == ".nsmp3" && codec < 3) codec = 3;

    std::vector<NsmpSectionInfo> sections;
    size_t o = bodyStart;
    while (o + headerSize <= bytes.size()) {
        uint32_t tagNum = legacy ? readU24BE_N(&bytes[o]) : readU32BE_N(&bytes[o]);
        uint32_t ver    = legacy ? readU16BE_N(&bytes[o + 3]) : readU32BE_N(&bytes[o + 4]);
        uint32_t sz     = readU32BE_N(&bytes[o + (legacy ? 5 : 8)]);

        std::string tagStr;
        for (int b = (legacy ? 2 : 3); b >= 0; --b) {
            char c = static_cast<char>((tagNum >> (b * 8)) & 0xff);
            tagStr.push_back((c >= 0x20 && c < 0x7f) ? c : '.');
        }

        size_t payloadOff = o + headerSize;
        if ((tagStr.find("NSMP") != std::string::npos || tagStr.find("NWS") != std::string::npos) &&
            sz > 64 && payloadOff + sz >= bytes.size() - 4) {
            o = payloadOff;
            continue;
        }
        if (payloadOff + sz > bytes.size()) break;
        sections.push_back({tagStr, ver, sz, payloadOff, payloadOff + sz});
        o = payloadOff + sz;
    }

    std::string instName = fs::path(filePath).stem().string();
    for (const auto& s : sections) {
        if (s.tag.size() >= 3 && s.tag.substr(s.tag.size() - 3) == "hdr") {
            std::string run = extractAsciiRun(bytes, s.payloadOffset, s.endOffset, 2);
            if (!run.empty()) instName = run;
            break;
        }
    }

    std::vector<const NsmpSectionInfo*> stkSections;
    for (const auto& s : sections) {
        if (s.tag.size() >= 3 && s.tag.substr(s.tag.size() - 3) == "stk" && s.size > 0x3c) {
            stkSections.push_back(&s);
        }
    }
    if (stkSections.empty()) return false;

    struct ParsedZone {
        int rootKey = 60, keyLow = 0, keyHigh = 127, velLow = 1, velTop = 127;
        uint32_t globalID = 0;
    };
    std::vector<ParsedZone> zones;

    for (const auto& s : sections) {
        if (s.tag.size() >= 3 && s.tag.substr(s.tag.size() - 3) == "map") {
            if (!legacy && (s.version >= 21 || codec == 4)) {
                codec = 4;
                size_t recStart = s.payloadOffset + 6 + 128 * 10 + 32;
                if (recStart < s.endOffset) {
                    int count = bytes[recStart - 1];
                    size_t cur = recStart;
                    for (int i = 0; i < count && cur + 16 <= s.endOffset; ++i) {
                        int nLayers = readU16BE_N(&bytes[cur + 6]);
                        if (nLayers < 1 || nLayers > 32 || cur + 8 + nLayers * 8 > s.endOffset) nLayers = 1;
                        for (int j = 0; j < nLayers; ++j) {
                            size_t e = cur + 8 + j * 8;
                            ParsedZone pz;
                            pz.rootKey  = bytes[cur + 0];
                            pz.keyHigh  = bytes[cur + 1];
                            pz.keyLow   = bytes[cur + 2];
                            pz.globalID = readU32BE_N(&bytes[e]);
                            pz.velLow   = std::max(1, static_cast<int>(bytes[e + 6]));
                            pz.velTop   = std::max(pz.velLow, static_cast<int>(bytes[e + 7]));
                            zones.push_back(pz);
                        }
                        cur += 8 + nLayers * 8;
                    }
                }
            } else if (!legacy && s.version >= 13) {
                size_t recStart = s.payloadOffset + 6 + 128 * 6 + 1;
                if (recStart < s.endOffset) {
                    int count = bytes[recStart - 1];
                    for (int i = 0; i < count && recStart + (i + 1) * 16 <= s.endOffset; ++i) {
                        size_t e = recStart + i * 16;
                        ParsedZone pz;
                        pz.rootKey  = bytes[e + 0];
                        pz.keyHigh  = bytes[e + 1];
                        pz.keyLow   = bytes[e + 2];
                        pz.globalID = readU32BE_N(&bytes[e + 8]);
                        pz.velLow   = std::max(1, static_cast<int>(bytes[e + 14]));
                        pz.velTop   = std::max(pz.velLow, static_cast<int>(bytes[e + 15]));
                        zones.push_back(pz);
                    }
                }
            }
            break;
        }
    }

    const bool wordInterleaved = (!legacy && codec == 4);
    outInstrument.id = g_nsmpInstId.fetch_add(1);
    outInstrument.name = instName;
    outInstrument.formatType = "NORD SAMPLE (" + ext + ")";
    outInstrument.sourcePath = filePath;
    outInstrument.strokes.clear();

    int strokeIdx = 0;
    for (const auto* stkSec : stkSections) {
        size_t pOff = stkSec->payloadOffset;
        size_t sEnd = stkSec->endOffset;
        uint32_t gid = readU32BE_N(&bytes[pOff]);
        int hdrRoot  = bytes[pOff + 5];
        uint16_t pitchBase = readU16BE_N(&bytes[pOff + 6]);
        uint32_t sr = (pitchBase >= 8000 && pitchBase <= 96000) ? pitchBase : static_cast<uint32_t>(NSMP_SAMPLE_RATE);
        int chHint = (bytes[pOff + 8] == 1 || bytes[pOff + 8] == 2) ? bytes[pOff + 8] : 1;

        DecodedNw1Stroke decoded;
        bool ok = false;

        if (legacy) {
            for (size_t start = pOff + 0x31; start < std::min(pOff + 0x260, sEnd); ++start) {
                uint32_t w = readU24BE_N(&bytes[start]);
                if (((w >> 14) & 0xf) == 0 && ((w >> 23) & 1) == 1) {
                    if (decodeNw1Stream(bytes, start, sEnd, chHint, false, true, decoded)) {
                        ok = true; break;
                    }
                }
            }
        } else {
            for (size_t hdr = 0x60; hdr <= 0x180 && pOff + hdr < sEnd; hdr += 4) {
                if (decodeNw1Stream(bytes, pOff + hdr, sEnd, chHint, wordInterleaved, false, decoded) &&
                    decoded.endOffset >= sEnd - 8) {
                    ok = true; break;
                }
            }
        }
        if (!ok || decoded.chL.empty()) continue;

        StrokeSample s;
        s.index = strokeIdx++;
        s.zone = s.index;
        s.actualRootMidi = (hdrRoot <= 127) ? hdrRoot : 60;
        s.storedRoot = s.actualRootMidi - 4;
        s.sampleRate = sr;
        s.frames = static_cast<uint32_t>(decoded.chL.size());

        bool matchedZone = false;
        for (const auto& pz : zones) {
            if (pz.globalID == gid || (pz.globalID & 0xff) == (gid & 0xff)) {
                s.actualRootMidi = pz.rootKey;
                s.lokey = pz.keyLow;
                s.hikey = pz.keyHigh;
                s.lovel = pz.velLow;
                s.hivel = pz.velTop;
                matchedZone = true;
                break;
            }
        }
        if (!matchedZone) {
            s.lokey = 0; s.hikey = 127; s.lovel = 1; s.hivel = 127;
        }

        int32_t maxPeak = 1;
        for (int32_t v : decoded.chL) maxPeak = std::max(maxPeak, std::abs(v));
        for (int32_t v : decoded.chR) maxPeak = std::max(maxPeak, std::abs(v));
        float scale = 1.0f / std::max<float>(8192.0f, static_cast<float>(maxPeak) * 1.15f);

        s.audioL.resize(s.frames);
        s.audioR.resize(s.frames);
        for (uint32_t i = 0; i < s.frames; ++i) {
            s.audioL[i] = static_cast<float>(decoded.chL[i]) * scale;
            s.audioR[i] = static_cast<float>(decoded.chR[i]) * scale;
        }

        if (pOff + 0x31 <= sEnd && !decoded.marks.empty()) {
            uint32_t u1 = readU32BE_N(&bytes[pOff + 0x12]);
            uint32_t u3 = readU32BE_N(&bytes[pOff + 0x24]);
            uint32_t u4 = readU32BE_N(&bytes[pOff + 0x2d]);
            if (u1 <= u3 && u3 < u4) {
                uint32_t loopBeginWord = u3 - u1;
                uint32_t endWord = u4 - u1;
                int32_t shift = static_cast<int32_t>(decoded.marks.back().word) - static_cast<int32_t>(endWord);
                for (const auto& mk : decoded.marks) {
                    if (static_cast<int32_t>(mk.word) == static_cast<int32_t>(loopBeginWord) + shift ||
                        mk.word == loopBeginWord) {
                        uint32_t lStart = mk.sample / std::max(1, decoded.channelCount);
                        if (lStart + 32 < s.frames) {
                            s.loop.hasLoop = true;
                            s.loop.start = lStart;
                            s.loop.end = s.frames;
                            s.loop.length = s.frames - lStart;
                            s.decayCoef = 0.999996f;
                        }
                        break;
                    }
                }
            }
        }
        outInstrument.strokes.push_back(std::move(s));
    }

    if (outInstrument.strokes.empty()) return false;

    if (outInstrument.strokes.size() > 1 &&
        outInstrument.strokes[0].lokey == 0 && outInstrument.strokes[0].hikey == 127) {
        std::sort(outInstrument.strokes.begin(), outInstrument.strokes.end(),
                  [](const StrokeSample& a, const StrokeSample& b) { return a.actualRootMidi < b.actualRootMidi; });
        for (size_t i = 0; i < outInstrument.strokes.size(); ++i) {
            outInstrument.strokes[i].lokey = (i == 0) ? 0 : (outInstrument.strokes[i - 1].actualRootMidi + outInstrument.strokes[i].actualRootMidi) / 2 + 1;
            outInstrument.strokes[i].hikey = (i + 1 == outInstrument.strokes.size()) ? 127 : (outInstrument.strokes[i].actualRootMidi + outInstrument.strokes[i + 1].actualRootMidi) / 2;
        }
    }

    outInstrument.zoneCount = static_cast<int>(outInstrument.strokes.size());
    size_t totalFloats = 0;
    for (const auto& st : outInstrument.strokes) totalFloats += st.audioL.size() + st.audioR.size();
    outInstrument.memoryMB = static_cast<double>(totalFloats * sizeof(float)) / (1024.0 * 1024.0);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    outStatusMsg = "Nord Sample decodificado: " + outInstrument.name +
                   " (" + std::to_string(outInstrument.strokes.size()) + " zonas @ 35kHz en " + std::to_string(ms) + " ms)";
    std::cout << "[NW1 Native] " << outStatusMsg << "\n";
    return true;
}

bool NsmpLoader::loadNsmpFallback(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg) {
    ClaviaHeaderInfo hdr = BankLoader::inspectClaviaHeader(filePath);
    outInstrument.id = g_nsmpInstId.fetch_add(1);
    outInstrument.headerInfo = hdr;
    outInstrument.name = hdr.displayName.empty() ? fs::path(filePath).stem().string() : hdr.displayName;
    outInstrument.formatType = hdr.formatCategory.empty() ? "NORD SAMPLE (.nsmp)" : hdr.formatCategory;
    outInstrument.sourcePath = filePath;
    outInstrument.channels = 2;
    outInstrument.zoneCount = 29;
    outInstrument.strokes.clear();

    const uint32_t sr = static_cast<uint32_t>(NSMP_SAMPLE_RATE);
    const uint32_t frames = sr * 2;
    int idx = 0;
    for (int k = 21; k <= 108; k += 3) {
        StrokeSample s;
        s.index = idx++;
        s.zone = s.index;
        s.actualRootMidi = k;
        s.lokey = (k == 21) ? 0 : (k - 1);
        s.hikey = (k + 3 > 108) ? 127 : (k + 1);
        s.frames = frames;
        s.sampleRate = sr;
        s.audioL.resize(frames);
        s.audioR.resize(frames);

        double f0 = 440.0 * std::pow(2.0, (k - 69.0) / 12.0);
        double period = sr / f0;
        uint32_t loopLen = static_cast<uint32_t>(std::round((sr * 1.2) / period) * period);
        s.loop = {true, false, sr / 2, loopLen, (sr / 2) + loopLen};
        s.decayCoef = 0.999995f;

        int maxH = std::min(16, static_cast<int>((sr * 0.42) / f0));
        for (uint32_t n = 0; n < frames; ++n) {
            float L = 0.0f, R = 0.0f;
            for (int h = 1; h <= maxH; ++h) {
                float amp = 1.0f / std::pow(static_cast<float>(h), 1.15f);
                L += static_cast<float>(std::sin(2.0 * 3.141592653589793 * (f0 * h) * n / sr + h * 0.4)) * amp;
                R += static_cast<float>(std::sin(2.0 * 3.141592653589793 * (f0 * h * 1.001) * n / sr + h * 1.1)) * amp;
            }
            s.audioL[n] = L * 0.25f;
            s.audioR[n] = R * 0.25f;
        }
        outInstrument.strokes.push_back(std::move(s));
    }
    outInstrument.memoryMB = (outInstrument.strokes.size() * frames * 2 * sizeof(float)) / (1024.0 * 1024.0);
    outStatusMsg = "Sample Synth cargado: " + outInstrument.name;
    return true;
}

} // namespace NordDSP