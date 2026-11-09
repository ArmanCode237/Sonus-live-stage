#include "NordSynthModule.h"
#include <algorithm>
#include <cstdlib>

namespace NordDSP {

NordSynthModule::NordSynthModule() {
    // Configurar valores por defecto iniciales para las 3 capas
    layers[0].params.enabled = true;
    layers[0].params.name = "Layer 1 (Saw Lead)";
    layers[0].params.waveform = SynthWaveform::Saw;

    layers[1].params.enabled = false;
    layers[1].params.name = "Layer 2 (Square Sub)";
    layers[1].params.waveform = SynthWaveform::Square;
    layers[1].params.octaveTranspose = -1;

    layers[2].params.enabled = false;
    layers[2].params.name = "Layer 3 (Pad Sine)";
    layers[2].params.waveform = SynthWaveform::Sine;
    layers[2].params.attackSec = 0.35f;
    layers[2].params.releaseSec = 1.2f;
}

void NordSynthModule::setSampleRate(double sr) {
    hostSampleRate_ = (sr > 8000.0) ? sr : 48000.0;
}

void SynthLayerEngine::noteOn(int midiKey, int velocity, double sampleRate) {
    if (!params.enabled) return;

    // Buscar voz libre o robar la más antigua
    SynthVoice* target = nullptr;
    for (auto& v : voices) {
        if (!v.active) { target = &v; break; }
    }
    if (!target) target = &voices[0];

    double adjustedKey = static_cast<double>(midiKey) +
                         (params.octaveTranspose * 12.0) +
                         static_cast<double>(params.semiTranspose) +
                         (params.detuneCents / 100.0);

    double freq = 440.0 * std::pow(2.0, (adjustedKey - 69.0) / 12.0);

    target->active = true;
    target->midiKey = midiKey;
    target->phase = 0.0;
    target->phaseInc = freq / sampleRate;
    target->envLevel = 0.0f;
    target->envStage = 1; // Attack
    target->velocity = static_cast<float>(velocity) / 127.0f;
}

void SynthLayerEngine::noteOff(int midiKey) {
    for (auto& v : voices) {
        if (v.active && v.midiKey == midiKey && v.envStage < 4) {
            v.envStage = 4; // Release
        }
    }
}

void SynthLayerEngine::allNotesOff() {
    for (auto& v : voices) v.active = false;
}

void SynthLayerEngine::render(float* outStereoInterleaved, unsigned int numFrames, double sampleRate) {
    if (!params.enabled || params.volume <= 0.001f) return;

    const float atkStep = 1.0f / (std::max(0.001f, params.attackSec) * static_cast<float>(sampleRate));
    const float decCoef = std::exp(-4.6f / (std::max(0.005f, params.decaySec) * static_cast<float>(sampleRate)));
    const float relCoef = std::exp(-6.9f / (std::max(0.005f, params.releaseSec) * static_cast<float>(sampleRate)));
    const float susLvl = std::clamp(params.sustainLevel, 0.0f, 1.0f);
    const float layerVol = params.volume;

    for (auto& v : voices) {
        if (!v.active) continue;

        for (unsigned int n = 0; n < numFrames; ++n) {
            // Generación ADSR
            switch (v.envStage) {
                case 1: // Attack
                    v.envLevel += atkStep;
                    if (v.envLevel >= 1.0f) { v.envLevel = 1.0f; v.envStage = 2; }
                    break;
                case 2: // Decay
                    v.envLevel = susLvl + (v.envLevel - susLvl) * decCoef;
                    if (std::abs(v.envLevel - susLvl) < 1e-4f) { v.envLevel = susLvl; v.envStage = 3; }
                    break;
                case 3: // Sustain
                    v.envLevel = susLvl;
                    break;
                case 4: // Release
                    v.envLevel *= relCoef;
                    if (v.envLevel < 1e-4f) {
                        v.envLevel = 0.0f;
                        v.active = false;
                    }
                    break;
                default:
                    break;
            }
            if (!v.active) break;

            // Generación de Forma de Onda
            float sampleOut = 0.0f;
            double p = v.phase;

            switch (params.waveform) {
                case SynthWaveform::Saw:
                    sampleOut = static_cast<float>(2.0 * p - 1.0);
                    break;
                case SynthWaveform::Square:
                    sampleOut = (p < 0.5) ? 0.6f : -0.6f;
                    break;
                case SynthWaveform::Triangle:
                    sampleOut = static_cast<float>(4.0 * std::abs(p - 0.5) - 1.0);
                    break;
                case SynthWaveform::Sine:
                    sampleOut = static_cast<float>(std::sin(2.0 * 3.141592653589793 * p));
                    break;
                case SynthWaveform::Noise:
                    sampleOut = static_cast<float>((std::rand() % 2000 - 1000) / 1000.0f) * 0.5f;
                    break;
            }

            float finalAmp = sampleOut * v.envLevel * v.velocity * layerVol;
            outStereoInterleaved[n * 2]     += finalAmp;
            outStereoInterleaved[n * 2 + 1] += finalAmp;

            v.phase += v.phaseInc;
            if (v.phase >= 1.0) v.phase -= 1.0;
        }
    }
}

void NordSynthModule::setSampleRate(double sr) {
    hostSampleRate_ = (sr > 8000.0) ? sr : 48000.0;
}

void NordSynthModule::noteOn(int midiKey, int velocity) {
    for (auto& layer : layers) {
        layer.noteOn(midiKey, velocity, hostSampleRate_);
    }
}

void NordSynthModule::noteOff(int midiKey) {
    for (auto& layer : layers) {
        layer.noteOff(midiKey);
    }
}

void NordSynthModule::allNotesOff() {
    for (auto& layer : layers) {
        layer.allNotesOff();
    }
}

void NordSynthModule::render(float* outInterleavedStereo, unsigned int numFrames) {
    float master = masterVolume_.load();
    for (auto& layer : layers) {
        layer.render(outInterleavedStereo, numFrames, hostSampleRate_);
    }
    // Atenuación general de seguridad del bus de sintetizador
    for (unsigned int i = 0; i < numFrames * 2; ++i) {
        outInterleavedStereo[i] *= master;
    }
}

} // namespace NordDSP