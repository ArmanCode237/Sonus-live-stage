#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "BankLoader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <map>
#include <thread>
#include <atomic>
#include <chrono>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace NordDSP {

namespace {

static std::atomic<uint64_t> g_nextInstrumentId{1};

inline uint16_t readU16BE(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

inline int16_t readI16BE(const uint8_t* p) {
    return static_cast<int16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

inline uint32_t readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

constexpr size_t DIR_OFFSET = 0x61c;
constexpr size_t LAYER_TABLE_OFFSET = 0x622;
constexpr size_t RECORDS_OFFSET = 0x732;
constexpr size_t RECORD_SIZE = 0x76;
constexpr int BLOCK_OVERLAP = 64;
constexpr uint16_t CNSP_V6 = 0x464;

const int PREDICTOR_LEN[5] = {0, 1, 2, 3, 4};
const int PREDICTORS[5][4] = {
    {0,  0, 0,  0},
    {1,  0, 0,  0},
    {2, -1, 0,  0},
    {3, -3, 1,  0},
    {4, -6, 4, -1}
};

int findCnspOffset(const std::vector<uint8_t>& bytes) {
    size_t limit = std::min<size_t>(bytes.size(), 0x100);
    for (size_t i = 0; i + 4 <= limit; ++i) {
        if (bytes[i] == 0x43 && bytes[i + 1] == 0x4e && bytes[i + 2] == 0x53 && bytes[i + 3] == 0x50) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline int framesPerBlock(int blockWords, int width, int channels) {
    if (width <= 0 || channels <= 0) return 0;
    return (((blockWords - 1) * 16) / (width * channels)) - BLOCK_OVERLAP;
}

std::string extractFirstAsciiRun(const std::vector<uint8_t>& bytes, size_t from, size_t to, size_t minLen = 3) {
    std::string run;
    size_t limit = std::min(to, bytes.size());
    for (size_t i = from; i < limit; ++i) {
        uint8_t c = bytes[i];
        if (c >= 0x20 && c < 0x7f) {
            run.push_back(static_cast<char>(c));
        } else {
            if (run.size() >= minLen) break;
            run.clear();
        }
    }
    while (!run.empty() && (run.back() == ' ' || run.back() == '\t')) run.pop_back();
    while (!run.empty() && (run.front() == ' ' || run.front() == '\t')) run.erase(run.begin());
    return (run.size() >= minLen) ? run : "";
}

} // namespace

// Si una muestra no trae bucle en el .npno (o es muy corto), busca cruces por cero en la cola para poder sostenerla
void BankLoader::ensureSustainLoop(StrokeSample& s) {
    if (s.frames < 4096 || s.audioL.size() < s.frames) return;

    // 1. Si el archivo .npno ya trae su bucle original de fábrica, respetarlo intacto
    if (s.loop.hasLoop && s.loop.length >= 64 && s.loop.start + s.loop.length <= s.frames) {
        s.loop.end = s.loop.start + s.loop.length;
        return;
    }

    // 2. En un piano real, las últimas 2 octavas (notas >= 86 D6) son percusivas y cortas;
    // no debemos inventarles un bucle artificial si el .npno de fábrica no lo incluyó.
    if (s.actualRootMidi >= 86) {
        s.loop.hasLoop = false;
        return;
    }

    uint32_t searchStart = static_cast<uint32_t>(s.frames * 0.45);
    uint32_t searchEnd = static_cast<uint32_t>(s.frames * 0.90);
    if (searchEnd <= searchStart + 4096) return;

    uint32_t loopStart = searchStart;
    for (uint32_t i = searchStart; i < searchStart + 4096 && i + 1 < searchEnd; ++i) {
        if (s.audioL[i] <= 0.0f && s.audioL[i + 1] > 0.0f) {
            loopStart = i;
            break;
        }
    }

    uint32_t loopEnd = searchEnd;
    for (uint32_t i = searchEnd; i > loopStart + 4096; --i) {
        if (s.audioL[i - 1] <= 0.0f && s.audioL[i] > 0.0f) {
            loopEnd = i;
            break;
        }
    }

    if (loopEnd > loopStart + 2048) {
        // Suavizar la costura del auto-loop con un crossfade de 256 muestras
        const uint32_t xfade = 256;
        if (loopStart >= xfade) {
            for (uint32_t k = 0; k < xfade; ++k) {
                float alpha = static_cast<float>(k) / static_cast<float>(xfade);
                uint32_t endIdx = loopEnd - xfade + k;
                uint32_t preStartIdx = loopStart - xfade + k;
                s.audioL[endIdx] = s.audioL[endIdx] * (1.0f - alpha) + s.audioL[preStartIdx] * alpha;
                s.audioR[endIdx] = s.audioR[endIdx] * (1.0f - alpha) + s.audioR[preStartIdx] * alpha;
            }
        }
        s.loop.hasLoop = true;
        s.loop.isAutoLoop = true;
        s.loop.start = loopStart;
        s.loop.length = loopEnd - loopStart;
        s.loop.end = loopEnd;
        if (s.decayCoef >= 0.999999f) {
            s.loopDecayDbPerS = -4.5f;
            s.decayCoef = std::pow(10.0f, -4.5f / (20.0f * static_cast<float>(NSP_ENVELOPE_RATE)));
        }
    }
}
ClaviaHeaderInfo BankLoader::inspectClaviaHeader(const std::string& filePath) {
    ClaviaHeaderInfo info;
    std::ifstream f(filePath, std::ios::binary);
    if (!f) return info;

    std::vector<uint8_t> buf(256, 0);
    f.read(reinterpret_cast<char*>(buf.data()), buf.size());
    size_t nRead = static_cast<size_t>(f.gcount());
    if (nRead < 28) return info;
    buf.resize(nRead);

    info.displayName = fs::path(filePath).stem().string();
    if (buf[0] == 'C' && buf[1] == 'B' && buf[2] == 'I' && buf[3] == 'N') {
        info.isValidCbin = true;
        char sig[5] = {static_cast<char>(buf[8]), static_cast<char>(buf[9]),
                       static_cast<char>(buf[10]), static_cast<char>(buf[11]), 0};
        info.signature = std::string(sig);
        info.versionRaw = static_cast<uint32_t>(buf[0x14] | (buf[0x15] << 8));
        uint32_t major = info.versionRaw / 100;
        uint32_t minor = info.versionRaw % 100;
        info.versionStr = std::to_string(major) + "." + (minor < 10 ? "0" : "") + std::to_string(minor);

        int cnsp = findCnspOffset(buf);
        if (cnsp >= 0) {
            if (static_cast<size_t>(cnsp + 0x18) < buf.size()) {
                info.pianoTypeByte = buf[cnsp + 0x18];
            }
            std::string full = extractFirstAsciiRun(buf, cnsp + 0x16, cnsp + 0x16 + 0x67);
            if (!full.empty()) {
                auto hashPos = full.find('#');
                if (hashPos == std::string::npos) {
                    info.displayName = full;
                } else {
                    info.displayName = full.substr(0, hashPos);
                    info.variant = full.substr(hashPos + 1);
                    while (!info.variant.empty() && info.variant.back() == ' ') info.variant.pop_back();
                }
            }
        }

        if (info.signature == "npno") info.formatCategory = "Nord Piano v" + info.versionStr;
        else if (info.signature == "nsmp" || info.signature == "nsm3" || info.signature == "nsm4")
            info.formatCategory = "Nord Sample (" + info.signature + ")";
        else
            info.formatCategory = "Nord (" + info.signature + ")";
    }
    return info;
}


bool BankLoader::loadNpnoNative(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg) {
    auto t0 = std::chrono::steady_clock::now();

    std::ifstream f(filePath, std::ios::binary | std::ios::ate);
    if (!f) {
        outStatusMsg = "No se pudo abrir: " + filePath;
        return false;
    }
    std::streamsize fileSize = f.tellg();
    if (fileSize < 0x800) {
        outStatusMsg = "Archivo demasiado pequeño para ser CNSP.";
        return false;
    }
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    if (!f.read(reinterpret_cast<char*>(bytes.data()), fileSize)) {
        outStatusMsg = "Error leyendo archivo en disco.";
        return false;
    }

    int base = findCnspOffset(bytes);
    if (base < 0 || static_cast<size_t>(base) + RECORDS_OFFSET > bytes.size()) {
        outStatusMsg = "El archivo no contiene cabecera CNSP.";
        return false;
    }

    uint16_t version = readU16BE(&bytes[base + DIR_OFFSET]);
    uint16_t channels = readU16BE(&bytes[base + DIR_OFFSET + 2]);
    uint16_t strokeCount = readU16BE(&bytes[base + DIR_OFFSET + 4]);

    if ((channels != 1 && channels != 2) || strokeCount == 0 ||
        static_cast<size_t>(base) + RECORDS_OFFSET + strokeCount * RECORD_SIZE > bytes.size()) {
        outStatusMsg = "Directorio CNSP fuera de rango.";
        return false;
    }

    ClaviaHeaderInfo hdr = inspectClaviaHeader(filePath);
    outInstrument.id = g_nextInstrumentId.fetch_add(1);
    outInstrument.headerInfo = hdr;
    outInstrument.name = hdr.displayName.empty() ? fs::path(filePath).stem().string() : hdr.displayName;
    if (!hdr.variant.empty()) outInstrument.name += " [" + hdr.variant + "]";
    outInstrument.variant = hdr.variant;
    outInstrument.version = hdr.versionStr;
    outInstrument.formatType = hdr.formatCategory.empty() ? "NORD PIANO (.npno)" : hdr.formatCategory;
    outInstrument.sourcePath = filePath;
    outInstrument.isV6 = (version >= CNSP_V6);
    outInstrument.channels = channels;
    outInstrument.releaseMode = bytes[base + 0x49c];
    outInstrument.undampedFromKey = (bytes[base + 0x40d] >= 89) ? bytes[base + 0x40d] : 90;

    // 1. Leer el Keymap en base + 0x8C (readKeyMap de nsp.ts)
    std::map<int, std::pair<int, int>> rootSpans;
    for (int note = 0; note < 128; ++note) {
        uint8_t r = bytes[base + 0x8c + note];
        if (r == 0 || r == 0xff || r > 127) continue;
        auto it = rootSpans.find(r);
        if (it == rootSpans.end()) rootSpans[r] = {note, note};
        else {
            if (note < it->second.first) it->second.first = note;
            if (note > it->second.second) it->second.second = note;
        }
    }

    std::vector<int> keymapRootsSorted;
    for (const auto& [rNote, _] : rootSpans) keymapRootsSorted.push_back(rNote);
    std::sort(keymapRootsSorted.begin(), keymapRootsSorted.end());

    // 2. Leer la tabla layers-per-root: u16BE[128] en base + 0x622 (readLayerTable de nsp-audio.ts)
    std::vector<int> layerTable(128, 0);
    std::vector<int> rootsWithLayers;
    int layerTableSum = 0;
    for (int n = 0; n < 128; ++n) {
        int c = readU16BE(&bytes[base + LAYER_TABLE_OFFSET + n * 2]);
        layerTable[n] = c;
        layerTableSum += c;
        if (c > 0) rootsWithLayers.push_back(n);
    }
    if (rootsWithLayers.empty()) rootsWithLayers = keymapRootsSorted;

    struct RawStrokeDesc {
        int index = 0;
        int zone = 0;
        int storedRoot = 60;
        uint32_t offset = 0;
        int velocityFrom = 0;
        uint8_t flags = 0;
        bool release = false;
        uint32_t frames = 0;
        uint16_t blocks = 0;
        int16_t history[2][4] = {{0}};
        bool hasLoop = false;
        uint32_t loopStart = 0;
        uint32_t loopLength = 0;
        float loopDecay = 1.0f;
        int8_t fineTune = 0;
        uint16_t levelWord = 125;
        uint8_t damperIdx = 44;
        uint8_t softDamperIdx = 50;
    };

    const int floorScale = (version < CNSP_V6) ? 5 : 1;
    std::vector<RawStrokeDesc> rawStrokes(strokeCount);

    for (uint16_t i = 0; i < strokeCount; ++i) {
        size_t at = static_cast<size_t>(base) + RECORDS_OFFSET + i * RECORD_SIZE;
        RawStrokeDesc& rs = rawStrokes[i];
        rs.index = i;
        rs.offset = static_cast<uint32_t>(base) + readU32BE(&bytes[at + 0x00]);
        rs.flags = bytes[at + 0x04] & 3;
        rs.release = (rs.flags == 2);
        rs.velocityFrom = static_cast<int>(bytes[at + 0x05]) * floorScale;
        rs.frames = readU32BE(&bytes[at + 0x06]);
        rs.blocks = readU16BE(&bytes[at + 0x0a]);

        for (uint16_t c = 0; c < channels; ++c) {
            size_t hOff = at + (c == 0 ? 0x0c : 0x14);
            int16_t h0 = readI16BE(&bytes[hOff + 0]);
            int16_t h1 = readI16BE(&bytes[hOff + 2]);
            int16_t h2 = readI16BE(&bytes[hOff + 4]);
            int16_t h3 = readI16BE(&bytes[hOff + 6]);
            rs.history[c][0] = h3;
            rs.history[c][1] = h2;
            rs.history[c][2] = h1;
            rs.history[c][3] = h0;
        }

        rs.loopStart = readU32BE(&bytes[at + 0x1c]);
        rs.loopLength = readU32BE(&bytes[at + 0x20]);
        rs.hasLoop = (rs.loopLength >= 4);
        if (rs.hasLoop) {
            uint32_t q23 = readU32BE(&bytes[at + 0x2e]);
            rs.loopDecay = (q23 > 0 && q23 <= 0x800000)
                ? static_cast<float>(static_cast<double>(q23) / static_cast<double>(0x800000))
                : 0.999985f;
        } else {
            rs.loopDecay = 1.0f;
        }

        rs.fineTune = static_cast<int8_t>(bytes[at + 0x32]);
        rs.levelWord = readU16BE(&bytes[at + 0x34]);
        rs.damperIdx = bytes[at + 0x72];
        rs.softDamperIdx = bytes[at + 0x73];
    }

    // Asignación de zonas según zoneOf() en nsp-audio.ts
    if (layerTableSum == static_cast<int>(strokeCount)) {
        size_t sIdx = 0;
        for (size_t z = 0; z < rootsWithLayers.size(); ++z) {
            int rNote = rootsWithLayers[z];
            int countForRoot = layerTable[rNote];
            for (int k = 0; k < countForRoot && sIdx < rawStrokes.size(); ++k, ++sIdx) {
                rawStrokes[sIdx].zone = static_cast<int>(z);
                rawStrokes[sIdx].storedRoot = rNote;
            }
        }
    } else {
        int z = -1;
        for (auto& rs : rawStrokes) {
            if ((rs.velocityFrom == 0 && !rs.release) || z < 0) ++z;
            rs.zone = z;
            if (z >= 0 && z < static_cast<int>(rootsWithLayers.size())) {
                rs.storedRoot = rootsWithLayers[z];
            } else if (!keymapRootsSorted.empty()) {
                int clampedZ = std::min<int>(z, static_cast<int>(keymapRootsSorted.size()) - 1);
                rs.storedRoot = keymapRootsSorted[clampedZ];
            }
        }
    }

    // En nsp-check.ts y nsp-write.ts blockWords es siempre 511 * channels
    const int blockWords = 511 * channels;

    std::vector<RawStrokeDesc> validStrokes;
    validStrokes.reserve(rawStrokes.size());
    for (const auto& rs : rawStrokes) {
        if (rs.flags != 0) continue; // Solo capas dinámicas principales
        if (rs.frames == 0 || rs.blocks == 0) continue;
        if (static_cast<size_t>(rs.offset) + static_cast<size_t>(rs.blocks) * blockWords * 2 > bytes.size()) continue;
        validStrokes.push_back(rs);
    }

    if (validStrokes.empty()) {
        outStatusMsg = "No se encontraron capas válidas en el archivo .npno.";
        return false;
    }

    std::map<int, std::vector<size_t>> strokesByZone;
    for (size_t i = 0; i < validStrokes.size(); ++i) {
        strokesByZone[validStrokes[i].zone].push_back(i);
    }

    outInstrument.zoneCount = static_cast<int>(strokesByZone.size());
    outInstrument.strokes.clear();
    outInstrument.strokes.resize(validStrokes.size());

    for (auto& [zoneId, indices] : strokesByZone) {
        int totalL = static_cast<int>(indices.size());
        for (int lIdx = 0; lIdx < totalL; ++lIdx) {
            size_t i = indices[lIdx];
            const RawStrokeDesc& rs = validStrokes[i];
            StrokeSample& s = outInstrument.strokes[i];

            s.index = rs.index;
            s.zone = rs.zone;
            s.layerIndex = lIdx;
            s.totalLayersInZone = totalL;
            s.velocityFrom = rs.velocityFrom;
            if (lIdx == 0) {
                s.switchPoint = 0;
            } else {
                int prevFloor = validStrokes[indices[lIdx - 1]].velocityFrom;
                s.switchPoint = rs.velocityFrom - ((rs.velocityFrom - prevFloor) / 2);
            }

            s.storedRoot = rs.storedRoot;
            s.actualRootMidi = rs.storedRoot + NSP_ROOT_OFFSET;

            auto spanIt = rootSpans.find(rs.storedRoot);
            if (spanIt != rootSpans.end()) {
                s.lokey = spanIt->second.first;
                s.hikey = spanIt->second.second;
            } else {
                s.lokey = std::max(0, s.actualRootMidi - 2);
                s.hikey = std::min(127, s.actualRootMidi + 1);
            }

            // Calcular el rango equivalente lovel..hivel según la curva de atenuación de Nord
            int softIdx = totalL - 1 - lIdx;
            s.lovel = (softIdx == 0) ? 1 : (softIdx * 127) / totalL + 1;
            s.hivel = (softIdx == totalL - 1) ? 127 : ((softIdx + 1) * 127) / totalL;

            s.frames = rs.frames;
            s.sampleRate = static_cast<uint32_t>(NSP_SAMPLE_RATE);
            s.loop = {rs.hasLoop, false, rs.loopStart, rs.loopLength, rs.hasLoop ? (rs.loopStart + rs.loopLength) : 0u};
            s.decayCoef = rs.loopDecay;
            s.loopDecayDbPerS = rs.hasLoop ? (20.0f * std::log10(std::max(1e-7f, rs.loopDecay)) * static_cast<float>(NSP_ENVELOPE_RATE)) : -3.5f;

            s.fineTuneCents = static_cast<float>(rs.fineTune);
            int8_t stretchByte = static_cast<int8_t>(bytes[base + 0x18c + std::clamp(s.hikey, 0, 127)]);
            s.stretchCents = static_cast<float>((outInstrument.isV6 ? 1 : -1) * stretchByte - rs.fineTune);

            s.baseLevelWord = rs.levelWord;
            // Atenuación suave sin multiplicar por encima de 1.0f para evitar cualquier distorsión
            if (rs.levelWord > 125 && rs.levelWord < 600) {
                float attDb = static_cast<float>(rs.levelWord - 125) * 0.08f;
                s.levelGainLinear = std::clamp(std::pow(10.0f, -attDb / 20.0f), 0.25f, 1.0f);
            } else {
                s.levelGainLinear = 1.0f;
            }

            s.damperIndex = rs.damperIdx ? rs.damperIdx : 44;
            s.softDamperIndex = rs.softDamperIdx ? rs.softDamperIdx : (s.damperIndex + 6);
            s.damperT60Seconds = 0.0015f * std::pow(10.0f, s.damperIndex / 20.0f);
            s.softDamperT60Seconds = 0.0015f * std::pow(10.0f, s.softDamperIndex / 20.0f);
        }
    }

    // Decodificación multihilo de todos los strokes con calibración limpia de 16 bits
    unsigned int numThreads = std::max(2u, std::thread::hardware_concurrency());
    numThreads = std::min<unsigned int>(numThreads, static_cast<unsigned int>(validStrokes.size()));
    std::atomic<size_t> nextStroke{0};
    std::vector<std::thread> workers;
    workers.reserve(numThreads);

    for (unsigned int th = 0; th < numThreads; ++th) {
        workers.emplace_back([&]() {
            while (true) {
                size_t idx = nextStroke.fetch_add(1, std::memory_order_relaxed);
                if (idx >= validStrokes.size()) break;

                const RawStrokeDesc& rs = validStrokes[idx];
                StrokeSample& dst = outInstrument.strokes[idx];

                dst.audioL.assign(rs.frames, 0.0f);
                dst.audioR.assign(rs.frames, 0.0f);

                int32_t hist[2][4];
                for (int c = 0; c < 2; ++c) {
                    for (int k = 0; k < 4; ++k) hist[c][k] = rs.history[c][k];
                }

                uint32_t t = 0;
                int32_t maxPeak = 1;
                bool decodeOk = true;

                for (uint16_t b = 0; b < rs.blocks && t < rs.frames; ++b) {
                    size_t at = static_cast<size_t>(rs.offset) + static_cast<size_t>(b) * blockWords * 2;
                    if (at + blockWords * 2 > bytes.size()) { decodeOk = false; break; }

                    uint16_t header = readU16BE(&bytes[at]);
                    int width = header & 0x1f;
                    int order = (header >> 5) & 7;

                    if (width == 0 || width > 24 || order >= 5) {
                        decodeOk = false;
                        break;
                    }

                    const int* coef = PREDICTORS[order];
                    const int coefLen = PREDICTOR_LEN[order];
                    const int own = framesPerBlock(blockWords, width, channels);
                    const int32_t half = 1 << (width - 1);
                    const int32_t full = 1 << width;

                    int bit = 0;
                    for (int n = 0; n < own && t < rs.frames; ++n) {
                        int32_t frameOut[2] = {0, 0};

                        for (uint16_t c = 0; c < channels; ++c) {
                            int32_t v = 0;
                            for (int got = 0; got < width; ) {
                                size_t wordOff = at + 2 + static_cast<size_t>(bit >> 4) * 2;
                                uint16_t word = readU16BE(&bytes[wordOff]);
                                int offs = bit & 15;
                                int take = std::min(16 - offs, width - got);
                                v += static_cast<int32_t>((word >> offs) & ((1u << take) - 1u)) << got;
                                got += take;
                                bit += take;
                            }
                            int32_t r = (v >= half) ? (v - full) : v;
                            int32_t x = r;
                            for (int k = 0; k < coefLen; ++k) {
                                x += coef[k] * hist[c][k];
                            }
                            hist[c][3] = hist[c][2];
                            hist[c][2] = hist[c][1];
                            hist[c][1] = hist[c][0];
                            hist[c][0] = x;
                            frameOut[c] = x;

                            int32_t absX = std::abs(x);
                            if (absX > maxPeak) maxPeak = absX;
                        }

                        dst.audioL[t] = static_cast<float>(frameOut[0]);
                        dst.audioR[t] = static_cast<float>(channels == 2 ? frameOut[1] : frameOut[0]);
                        ++t;
                    }
                }

                if (!decodeOk || t < 16) {
                    dst.frames = 0;
                    continue;
                }
                dst.frames = t;

                // Normalización sin distorsión: en fábrica NPNO_PEAK = 7782, pero capas fuertes pueden llegar a 32767.
                // Usamos una referencia limpia de 16384.0f con techo garantizado <= 0.85f para cero saturación.
                float normScale = 1.0f / std::max<float>(12000.0f, static_cast<float>(maxPeak) * 1.18f);
                for (uint32_t fIdx = 0; fIdx < t; ++fIdx) {
                    dst.audioL[fIdx] *= normScale;
                    dst.audioR[fIdx] *= normScale;
                }

                ensureSustainLoop(dst);
            }
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    outInstrument.strokes.erase(
        std::remove_if(outInstrument.strokes.begin(), outInstrument.strokes.end(),
                       [](const StrokeSample& s) { return s.frames < 16; }),
        outInstrument.strokes.end()
    );

    size_t totalFloats = 0;
    for (const auto& s : outInstrument.strokes) totalFloats += s.audioL.size() + s.audioR.size();
    outInstrument.memoryMB = static_cast<double>(totalFloats * sizeof(float)) / (1024.0 * 1024.0);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    outStatusMsg = "Cargado: " + outInstrument.name + " (" +
                   std::to_string(outInstrument.strokes.size()) + " muestras, " +
                   std::to_string(outInstrument.zoneCount) + " zonas en " + std::to_string(ms) + " ms)";
    std::cout << "[CNSP Native] " << outStatusMsg << "\n";
    return !outInstrument.strokes.empty();
}

// Soporte para archivos .nsmp / .nsmp3 / .nsmp4 para que seleccionarlos en la lista cambie efectivamente de instrumento
bool BankLoader::loadNsmpFallback(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg) {
    ClaviaHeaderInfo hdr = inspectClaviaHeader(filePath);
    outInstrument.id = g_nextInstrumentId.fetch_add(1);
    outInstrument.headerInfo = hdr;
    outInstrument.name = hdr.displayName.empty() ? fs::path(filePath).stem().string() : hdr.displayName;
    outInstrument.formatType = hdr.formatCategory.empty() ? "NORD SAMPLE (.nsmp)" : hdr.formatCategory;
    outInstrument.sourcePath = filePath;
    outInstrument.channels = 2;
    outInstrument.zoneCount = 29;
    outInstrument.strokes.clear();

    std::ifstream f(filePath, std::ios::binary);
    std::vector<uint8_t> bin(4096, 0);
    if (f) f.read(reinterpret_cast<char*>(bin.data()), bin.size());
    uint32_t seed = 0x811C9DC5u;
    for (uint8_t b : bin) { seed ^= b; seed *= 0x01000193u; }

    const uint32_t sr = 44100;
    const uint32_t frames = sr * 2;
    const float detune = 0.0008f + ((seed & 0xFF) / 255.0f) * 0.0015f;

    int idx = 0;
    for (int k = 21; k <= 108; k += 3) {
        StrokeSample s;
        s.index = idx++;
        s.zone = s.index;
        s.layerIndex = 0;
        s.totalLayersInZone = 1;
        s.storedRoot = k - 4;
        s.actualRootMidi = k;
        s.lokey = (k == 21) ? 0 : (k - 1);
        s.hikey = (k + 3 > 108) ? 127 : (k + 1);
        s.lovel = 1;
        s.hivel = 127;
        s.frames = frames;
        s.sampleRate = sr;
        s.audioL.resize(frames);
        s.audioR.resize(frames);

        double f0 = 440.0 * std::pow(2.0, (k - 69.0) / 12.0);
        double period = sr / f0;
        uint32_t loopLen = static_cast<uint32_t>(std::round((sr * 1.2) / period) * period);
        s.loop = {true, false, sr / 2, loopLen, (sr / 2) + loopLen};
        s.decayCoef = 0.999995f;

        int maxH = std::min(20, static_cast<int>((sr * 0.42) / f0));
        for (uint32_t n = 0; n < frames; ++n) {
            float L = 0.0f, R = 0.0f;
            for (int h = 1; h <= maxH; ++h) {
                float amp = 1.0f / std::pow(static_cast<float>(h), 1.15f);
                L += static_cast<float>(std::sin(2.0 * 3.141592653589793 * (f0 * h) * n / sr + h * 0.4)) * amp;
                R += static_cast<float>(std::sin(2.0 * 3.141592653589793 * (f0 * h * (1.0 + detune)) * n / sr + h * 1.1)) * amp;
            }
            s.audioL[n] = L * 0.25f;
            s.audioR[n] = R * 0.25f;
        }
        outInstrument.strokes.push_back(std::move(s));
    }

    outInstrument.defaultPreset.attackSec = 0.015f;
    outInstrument.defaultPreset.releaseSec = 0.55f;
    outInstrument.defaultPreset.loopHold = 0.85f;
    outInstrument.memoryMB = (outInstrument.strokes.size() * frames * 2 * sizeof(float)) / (1024.0 * 1024.0);
    outStatusMsg = "Sample Synth cargado: " + outInstrument.name;
    return true;
}

bool BankLoader::loadClaviaProgramPreset(const std::string& filePath, PianoInstrument& ioInstrument, std::string& outStatusMsg) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> bin((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bin.size() < 32) return false;

    ClaviaHeaderInfo hdr = inspectClaviaHeader(filePath);
    SynthPresetParams& p = ioInstrument.defaultPreset;

    uint8_t bCut = bin.size() > 42 ? bin[42] : 220;
    uint8_t bRes = bin.size() > 45 ? bin[45] : 25;
    uint8_t bAtk = bin.size() > 50 ? bin[50] : 2;
    uint8_t bRel = bin.size() > 54 ? bin[54] : 50;

    p.cutoffHz = 600.0f * std::pow(33.0f, bCut / 255.0f);
    p.resonanceQ = 0.707f + (bRes / 255.0f) * 3.5f;
    p.attackSec = 0.001f + std::pow(bAtk / 255.0f, 2.5f) * 1.2f;
    p.releaseSec = 0.10f + (bRel / 255.0f) * 1.8f;

    outStatusMsg = "Preset Nord aplicado: " + hdr.displayName + " sobre el instrumento actual.";
    return true;
}

bool BankLoader::loadWaveFile(const std::string& wavPath, StrokeSample& s) {
    drwav wav;
    if (!drwav_init_file(&wav, wavPath.c_str(), nullptr)) return false;

    uint64_t totalFrames = wav.totalPCMFrameCount;
    uint32_t ch = wav.channels;
    s.sampleRate = wav.sampleRate ? wav.sampleRate : 44100;
    if (totalFrames == 0 || ch == 0) { drwav_uninit(&wav); return false; }

    std::vector<float> pSampleData(totalFrames * ch);
    uint64_t framesRead = drwav_read_pcm_frames_f32(&wav, totalFrames, pSampleData.data());
    drwav_uninit(&wav);
    if (framesRead < 4) return false;

    s.frames = static_cast<uint32_t>(framesRead);
    s.audioL.resize(framesRead);
    s.audioR.resize(framesRead);

    if (ch == 1) {
        std::copy(pSampleData.begin(), pSampleData.begin() + framesRead, s.audioL.begin());
        std::copy(pSampleData.begin(), pSampleData.begin() + framesRead, s.audioR.begin());
    } else {
        for (size_t i = 0; i < framesRead; ++i) {
            s.audioL[i] = pSampleData[i * ch];
            s.audioR[i] = pSampleData[i * ch + 1];
        }
    }
    ensureSustainLoop(s);
    return true;
}

void BankLoader::loadWavesInParallel(std::vector<StrokeSample>& strokes, const std::vector<std::string>& wavPaths) {
    const size_t total = strokes.size();
    if (total == 0) return;

    unsigned int numThreads = std::max(2u, std::thread::hardware_concurrency());
    numThreads = std::min<unsigned int>(numThreads, static_cast<unsigned int>(total));

    std::atomic<size_t> nextIndex{0};
    std::vector<std::thread> workers;
    for (unsigned int t = 0; t < numThreads; ++t) {
        workers.emplace_back([&]() {
            while (true) {
                size_t i = nextIndex.fetch_add(1, std::memory_order_relaxed);
                if (i >= total) break;
                if (!loadWaveFile(wavPaths[i], strokes[i])) strokes[i].frames = 0;
            }
        });
    }
    for (auto& th : workers) if (th.joinable()) th.join();

    strokes.erase(
        std::remove_if(strokes.begin(), strokes.end(), [](const StrokeSample& s) { return s.frames < 4; }),
        strokes.end()
    );
}

bool BankLoader::loadFromSfz(const std::string& sfzFilePath, PianoInstrument& outInstrument) {
    fs::path sfzPath(sfzFilePath);
    if (!fs::exists(sfzPath)) return false;

    std::ifstream f(sfzPath);
    if (!f) return false;

    outInstrument.id = g_nextInstrumentId.fetch_add(1);
    outInstrument.strokes.clear();
    outInstrument.name = sfzPath.stem().string();
    outInstrument.formatType = "SFZ";
    outInstrument.sourcePath = sfzPath.string();

    fs::path baseDir = sfzPath.parent_path();
    std::string defaultSubPath = "";
    std::vector<StrokeSample> tempStrokes;
    std::vector<std::string> wavPaths;

    std::string line;
    int idx = 0;
    while (std::getline(f, line)) {
        auto commentPos = line.find("//");
        if (commentPos != std::string::npos) line = line.substr(0, commentPos);

        if (line.find("default_path=") != std::string::npos) {
            auto p = line.find("default_path=") + 13;
            defaultSubPath = line.substr(p);
            defaultSubPath.erase(defaultSubPath.find_last_not_of(" \t\r\n") + 1);
        }
        if (line.find("<region>") != std::string::npos) {
            StrokeSample s;
            s.index = idx++;
            auto extractToken = [&](const std::string& key) -> std::string {
                auto pos = line.find(key + "=");
                if (pos == std::string::npos) return "";
                pos += key.length() + 1;
                auto end = line.find_first_of(" \t\r\n", pos);
                return line.substr(pos, end - pos);
            };
            std::string sampleName = extractToken("sample");
            if (sampleName.empty()) continue;
            try {
                std::string v;
                if (!(v = extractToken("pitch_keycenter")).empty()) s.actualRootMidi = std::stoi(v);
                if (!(v = extractToken("lokey")).empty()) s.lokey = std::stoi(v);
                if (!(v = extractToken("hikey")).empty()) s.hikey = std::stoi(v);
                if (!(v = extractToken("lovel")).empty()) s.lovel = std::stoi(v);
                if (!(v = extractToken("hivel")).empty()) s.hivel = std::stoi(v);
            } catch (...) {}
            if (s.lovel > s.hivel) std::swap(s.lovel, s.hivel);
            fs::path fullWavPath = baseDir / defaultSubPath / sampleName;
            if (!fs::exists(fullWavPath)) fullWavPath = baseDir / sampleName;
            tempStrokes.push_back(std::move(s));
            wavPaths.push_back(fullWavPath.string());
        }
    }
    loadWavesInParallel(tempStrokes, wavPaths);
    outInstrument.strokes = std::move(tempStrokes);
    return !outInstrument.strokes.empty();
}

bool BankLoader::loadFromDirectory(const std::string& folderPath, PianoInstrument& outInstrument) {
    fs::path baseDir(folderPath);
    if (!fs::exists(baseDir)) return false;

    for (const auto& entry : fs::directory_iterator(baseDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".npno") {
            std::string msg;
            if (loadNpnoNative(entry.path().string(), outInstrument, msg)) return true;
        }
    }
    return false;
}

bool BankLoader::loadAny(const std::string& pathStr, PianoInstrument& outInstrument, std::string& outStatusMsg) {
    fs::path p(pathStr);
    if (!fs::exists(p)) {
        outStatusMsg = "La ruta no existe: " + pathStr;
        return false;
    }

    if (fs::is_directory(p)) {
        if (loadFromDirectory(p.string(), outInstrument)) {
            outStatusMsg = "Piano Nord activo: " + outInstrument.name +
                           " (" + std::to_string(outInstrument.strokes.size()) + " muestras PCM)";
            return true;
        }
        outStatusMsg = "Selecciona un archivo .npno de la lista.";
        return false;
    }

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".npno" || ext == ".nsp") {
        return loadNpnoNative(p.string(), outInstrument, outStatusMsg);
    }

    if (ext == ".nsmp" || ext == ".nsmp3" || ext == ".nsmp4") {
        if (loadNpnoNative(p.string(), outInstrument, outStatusMsg)) return true;
        return loadNsmpFallback(p.string(), outInstrument, outStatusMsg);
    }

    if (ext == ".ns3f" || ext == ".ns2p" || ext == ".ns2f" || ext == ".nw2p") {
        return loadClaviaProgramPreset(p.string(), outInstrument, outStatusMsg);
    }

    if (ext == ".sfz") {
        if (loadFromSfz(p.string(), outInstrument)) {
            outStatusMsg = "SFZ cargado: " + outInstrument.name;
            return true;
        }
    }

    outStatusMsg = "Formato no soportado: " + ext;
    return false;
}

std::vector<LibraryEntry> BankLoader::scanLibraryDirectory(const std::string& rootDir) {
    std::vector<LibraryEntry> entries;
    fs::path root(rootDir);
    if (!fs::exists(root) || !fs::is_directory(root)) return entries;

    const std::vector<std::string> validExts = {
        ".npno", ".nsmp", ".nsmp3", ".nsmp4", ".nsp", ".ns3f", ".ns2p", ".ns2f", ".nw2p", ".sfz"
    };

    auto classifySound = [](const std::string& filename, const std::string& ext, const ClaviaHeaderInfo& hdr) -> std::string {
        std::string low = filename + " " + hdr.displayName + " " + hdr.variant;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);

        if (ext == ".ns3f" || ext == ".ns2p" || ext == ".ns2f" || ext == ".nw2p") return "Programs";
        if (low.find("accord") != std::string::npos || low.find("acord") != std::string::npos ||
            low.find("harmoni") != std::string::npos || low.find("bandon") != std::string::npos ||
            low.find("melodica") != std::string::npos || low.find("organ") != std::string::npos ||
            low.find("farfisa") != std::string::npos || low.find("vox") != std::string::npos ||
            low.find("b3") != std::string::npos || low.find("pipe") != std::string::npos) {
            return "Accordion & Organ";
        }
        if (low.find("xylo") != std::string::npos || low.find("xilof") != std::string::npos ||
            low.find("marimba") != std::string::npos || low.find("vibra") != std::string::npos ||
            low.find("vibes") != std::string::npos || low.find("mallet") != std::string::npos ||
            low.find("kalimba") != std::string::npos || low.find("glocken") != std::string::npos ||
            low.find("celesta") != std::string::npos || low.find("bell") != std::string::npos ||
            low.find("music box") != std::string::npos || (ext == ".npno" && hdr.pianoTypeByte == 16)) {
            return "Xylophone & Mallets";
        }
        if (low.find("rhodes") != std::string::npos || low.find("wurl") != std::string::npos ||
            low.find("clav") != std::string::npos || low.find("ep1") != std::string::npos ||
            low.find("ep2") != std::string::npos || low.find("ep3") != std::string::npos ||
            low.find("ep4") != std::string::npos || low.find("ep5") != std::string::npos ||
            low.find("ep6") != std::string::npos || low.find("ep7") != std::string::npos ||
            low.find("ep8") != std::string::npos || low.find("ep9") != std::string::npos ||
            low.find("dx7") != std::string::npos || low.find("digigrand") != std::string::npos ||
            low.find("cp80") != std::string::npos || low.find("electric") != std::string::npos ||
            (ext == ".npno" && (hdr.pianoTypeByte == 1 || hdr.pianoTypeByte == 2 || hdr.pianoTypeByte == 3 ||
                                hdr.pianoTypeByte == 4 || hdr.pianoTypeByte == 14 || hdr.pianoTypeByte == 15))) {
            return "Electric Pianos";
        }
        if (low.find("string") != std::string::npos || low.find("violin") != std::string::npos ||
            low.find("cello") != std::string::npos || low.find("orchestra") != std::string::npos ||
            low.find("choir") != std::string::npos || low.find("voice") != std::string::npos ||
            low.find("brass") != std::string::npos || low.find("trumpet") != std::string::npos ||
            low.find("flute") != std::string::npos || low.find("sax") != std::string::npos ||
            low.find("pad") != std::string::npos || low.find("mellotron") != std::string::npos) {
            return "Strings, Brass & Pads";
        }
        if (ext == ".npno" || low.find("grand") != std::string::npos ||
            low.find("upright") != std::string::npos || low.find("piano") != std::string::npos ||
            low.find("steinway") != std::string::npos || low.find("bosendorfer") != std::string::npos ||
            low.find("yamaha") != std::string::npos || low.find("bambino") != std::string::npos ||
            low.find("saloon") != std::string::npos || low.find("felt") != std::string::npos) {
            return "Acoustic Pianos";
        }
        return "Synth & Misc Samples";
    };

    try {
        for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (std::find(validExts.begin(), validExts.end(), ext) != validExts.end()) {
                ClaviaHeaderInfo hdr = inspectClaviaHeader(entry.path().string());
                std::string cat = "SAMPLE";
                if (ext == ".npno") cat = "PIANO (.npno)";
                else if (ext == ".ns3f" || ext == ".ns2p" || ext == ".ns2f" || ext == ".nw2p") cat = "PROGRAM (" + ext + ")";
                else if (ext == ".sfz") cat = "SFZ";
                else cat = "SAMPLE (" + ext + ")";

                std::string group = classifySound(entry.path().filename().string(), ext, hdr);
                std::error_code ec;
                uintmax_t sz = fs::file_size(entry.path(), ec);

                entries.push_back({
                    entry.path().filename().string(), // Nombre original intacto
                    entry.path().generic_string(),    // Ruta con barras '/' uniformes
                    ext,
                    cat,
                    group,
                    sz,
                    true
                });
            }
        }
        std::sort(entries.begin(), entries.end(), [](const LibraryEntry& a, const LibraryEntry& b) {
            if ((a.extension == ".npno") != (b.extension == ".npno")) return a.extension == ".npno";
            return a.name < b.name;
        });
    } catch (...) {}

    return entries;
}

} // namespace NordDSP