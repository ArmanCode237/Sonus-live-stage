"""Vintage-inspired synth factory bank (IDs 385-432), v31.

Original parameter designs for this engine, inspired by the signature sounds of six
classic instruments. No samples, ROM data or third-party patch data are used: every
value below is written for the Live Stage synth (FM4, LA hybrid, 24 dB filters,
analog drift, attack transients).

    python tools/build_vintage_presets.py [--gains gains.json]

Writes presets/synth/factory/385.json … 432.json and appends the catalogue entries
to webui/src/data/synthFactory.ts. --gains is a {"id": multiplier} file produced by
the loudness check (tests/v31_vintage_presets.cpp) to level-match the bank.
"""
from __future__ import annotations
import copy, json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FACTORY = ROOT / 'presets/synth/factory'
CATALOG = ROOT / 'webui/src/data/synthFactory.ts'
FIRST_ID = 385

# ------------------------------------------------------------------ building blocks
FX = {'enabled': False, 'rateHz': .5, 'depth': .5, 'mix': .3, 'feedback': .15, 'timeMs': 280, 'syncEnabled': False, 'syncDivision': 5}
LAYER = {
    'enabled': True, 'synthesisType': 0, 'waveType': 0, 'volume': .6, 'pan': 0.0, 'octave': 0, 'semitones': 0,
    'detuneCents': 0.0, 'cutoffHz': 8000.0, 'resonanceQ': .8, 'attackSec': .005, 'decaySec': 1.0, 'sustainLevel': 1.0,
    'releaseSec': .3, 'unisonVoices': 1, 'unisonDetune': 8.0, 'unisonSpread': .5, 'phaseOffset': 0.0, 'keyTrack': 1.0,
    'velToCutoff': .2, 'velToAmp': .8, 'wavetableIdx': 0, 'wavetableMorph': 0.0, 'fmIndex': 2.0, 'fmRatio': 2.0,
    'filterType': 5, 'filterEnvAmt': 0.0, 'filterKeyTrack': .5, 'filtAttackSec': .005, 'filtDecaySec': .5,
    'filtSustainLvl': .5, 'filtReleaseSec': .3, 'sawLevel': 1.0, 'pulseLevel': 0.0, 'pulseWidth': .5, 'pwmDepth': 0.0,
    'pwmRateHz': .6, 'subLevel': 0.0, 'noiseLevel': 0.0, 'osc2Level': 0.0, 'osc2Detune': 0.0, 'osc2Semitones': 0,
    'osc2Wave': 0, 'highpassHz': 20.0, 'drive': 0.0, 'voiceMode': 0, 'portamentoEnabled': False, 'portamento': 0.0,
    'glideMode': 1, 'slide': 0.0, 'wheelMode': 0, 'wheelCutoffOctaves': 0.0, 'wheelAmpDb': 0.0,
    'wheelVibratoCents': 30.0, 'wheelVibratoRateHz': 5.2, 'wheelWtMorph': 0.0, 'seqEnabled': False, 'seqBpm': 120.0,
    'seqSteps': 16, 'seqGate': .8, 'seqNotes': [0] * 16, 'keyLow': 0, 'keyHigh': 127, 'keyTranspose': 0,
    'insertFx': [dict(FX) for _ in range(5)],
    'analog': 0.0, 'fm4Algorithm': 0, 'fm4Feedback': 0.0, 'fm4VelMod': .5,
    'opRatio': [1.0, 1.0, 1.0, 1.0], 'opLevel': [1.0, .5, .5, .3], 'opAttack': [.002] * 4,
    'opDecay': [1.5, .8, .8, .8], 'opSustain': [.6, .3, .3, .3],
    'laMix': .5, 'laRing': 0.0, 'transientType': 0, 'transientLevel': 0.0, 'transientDecay': .08, 'transientTone': .5,
    'mt32Program': 0,
}
LFO = {'enabled': False, 'waveType': 0, 'rateHz': 5.0, 'depth': .5, 'delaySec': 0.0, 'fadeSec': 0.0, 'destPitch': 0,
       'destCutoff': 0, 'destVolume': 0.0, 'destPan': 0.0, 'destPhase': 0.0, 'keySync': False}
CHORUS_I, CHORUS_II, PHASER_I, PHASER_II, ECHO = range(5)
SILENT_FX = {k: False for k in ('chorusEnabled', 'delayEnabled', 'reverbEnabled', 'driveEnabled', 'autoPanEnabled',
                                 'phaserEnabled', 'tremoloEnabled', 'flangerEnabled')}


def layer(**kw):
    out = copy.deepcopy(LAYER)
    inserts = kw.pop('fx', {})
    for key, value in kw.items():
        assert key in out, key
        out[key] = value
    for slot, params in inserts.items():
        out['insertFx'][slot].update({'enabled': True, **params})
    return out


def lfo(**kw):
    out = dict(LFO); out.update(kw, enabled=True); return out


def patch(name, category, inspiration, layers, lfos=(), reverb=0.0, hall=True, delay=None, mono=None, vol=.82):
    """mono = (legato: bool, glide seconds) for the whole synth."""
    layers = list(layers) + [layer(enabled=False) for _ in range(4 - len(layers))]
    fx = dict(SILENT_FX)
    if reverb > 0:
        fx.update(reverbEnabled=True, reverbType=2 if hall else 1, reverbMix=reverb, reverbSize=.72 if hall else .5)
    if delay:
        fx.update(delayEnabled=True, delayMs=delay[0], delayFeedback=delay[1], delayMix=delay[2])
    params = {
        'synthEnabled': True, 'synthMasterVol': vol, 'synthMacroCutoff': 1.0, 'synthWheelGlobalCutoff': 0.0,
        'synthWheelGlobalAmpDb': 0.0, 'layers': layers,
        'lfos': list(lfos) + [dict(LFO) for _ in range(4 - len(lfos))],
        'synthVoiceMaster': True, 'synthVoiceMode': 0, 'synthPortamentoEnabled': False, 'synthPortamento': 0.0,
        'synthGlideMode': 1, 'synthSlide': 0.0, 'synthFx': fx, 'synthFxSends': [],
    }
    if mono:
        params.update(synthVoiceMode=2 if mono[0] else 1, synthPortamentoEnabled=mono[1] > 0, synthPortamento=mono[1])
    return {'name': name, 'category': category, 'inspiration': inspiration, 'params': params}


def fm4(algo, ratios, levels, attacks, decays, sustains, **kw):
    return layer(synthesisType=3, fm4Algorithm=algo, opRatio=ratios, opLevel=levels, opAttack=attacks,
                 opDecay=decays, opSustain=sustains, filterType=0, cutoffHz=20000.0, attackSec=.001,
                 decaySec=1.0, sustainLevel=1.0, velToCutoff=0.0, **kw)


# ------------------------------------------------------------------ the bank
BANK = []
D50, DX7, JP8, JUNO, P5, MINI = 'D-50', 'DX7', 'JUPITER-8', 'JUNO-60', 'PROPHET-5', 'MINIMOOG'

# Roland D-50 style: LA hybrid (PCM-style attack transient + looped digital partial), chorus, reverb.
BANK += [
    patch('D-50 · Fantasy Bell Pad', D50, 'LA bell attack over a breathing digital pad', [
        layer(synthesisType=4, wavetableIdx=5, wavetableMorph=.3, laMix=.35, sawLevel=.5, pulseLevel=.3,
              transientType=5, transientLevel=.7, transientDecay=.9, transientTone=.7, cutoffHz=5200, resonanceQ=1.0,
              attackSec=.25, decaySec=2.0, sustainLevel=.8, releaseSec=2.5, volume=.6, analog=.1,
              fx={CHORUS_II: {'mix': .45, 'rateHz': .5, 'depth': .55}}),
        layer(synthesisType=4, wavetableIdx=26, laMix=.0, octave=1, transientType=1, transientLevel=.5,
              transientDecay=1.2, transientTone=.8, attackSec=.002, decaySec=1.5, sustainLevel=0.0, releaseSec=1.5,
              volume=.38, pan=.25)],
          reverb=.35, delay=(375, .35, .18)),
    patch('D-50 · Breath Choir', D50, 'breathy vocal partial with a noise attack', [
        layer(synthesisType=4, wavetableIdx=38, wavetableMorph=.3, laMix=.15, sawLevel=.4,
              transientType=0, transientLevel=.6, transientDecay=.35, transientTone=.4, cutoffHz=4200,
              attackSec=.4, decaySec=1.5, sustainLevel=.85, releaseSec=2.0, volume=.7, analog=.15,
              fx={CHORUS_I: {'mix': .4, 'rateHz': .45, 'depth': .5}})],
          lfos=[lfo(waveType=1, rateHz=.25, depth=.6, destPitch=6, delaySec=.3, fadeSec=.8)], reverb=.4),
    patch('D-50 · Staccato Heaven', D50, 'mallet partials with a soft choir bed', [
        layer(synthesisType=4, wavetableIdx=42, laMix=.1, transientType=1, transientLevel=.8, transientDecay=.5,
              transientTone=.7, attackSec=.001, decaySec=.8, sustainLevel=0.0, releaseSec=.6, volume=.65,
              fx={ECHO: {'mix': .25, 'feedback': .35, 'syncEnabled': True, 'syncDivision': 8}}),
        layer(synthesisType=4, wavetableIdx=29, laMix=.2, attackSec=.6, sustainLevel=.7, releaseSec=2.0,
              cutoffHz=3800, volume=.35)],
          reverb=.4),
    patch('D-50 · Digital Dance Sequence', D50, 'plucked digital partial driven by the step sequencer', [
        layer(synthesisType=4, wavetableIdx=40, laMix=.3, sawLevel=.6, pulseLevel=.4, transientType=2,
              transientLevel=.8, transientDecay=.12, transientTone=.6, attackSec=.001, decaySec=.35, sustainLevel=.15,
              releaseSec=.2, cutoffHz=4500, seqEnabled=True, seqBpm=118, seqSteps=16, seqGate=.45,
              seqNotes=[0, 12, 7, 0, 10, 12, 3, 7, 0, 12, 7, 15, 10, 7, 5, 3], volume=.65,
              fx={ECHO: {'mix': .2, 'feedback': .3, 'syncEnabled': True, 'syncDivision': 8}}),
        layer(synthesisType=4, wavetableIdx=34, laMix=.25, attackSec=.8, sustainLevel=.7, releaseSec=2.4,
              cutoffHz=2600, volume=.3, fx={CHORUS_II: {'mix': .4}})],
          reverb=.3),
    patch('D-50 · Glass Voices', D50, 'glass partials ring-modulated over a vocal table', [
        layer(synthesisType=4, wavetableIdx=8, laMix=.2, laRing=.15, sawLevel=.3, transientType=5,
              transientLevel=.5, transientDecay=.7, transientTone=.8, attackSec=.3, decaySec=2.0, sustainLevel=.8,
              releaseSec=2.5, volume=.55, fx={CHORUS_II: {'mix': .45}}),
        layer(synthesisType=4, wavetableIdx=38, laMix=.0, attackSec=.9, sustainLevel=.8, releaseSec=2.8,
              cutoffHz=3500, volume=.4, pan=-.2)],
          lfos=[lfo(waveType=1, rateHz=.18, depth=.5, destPitch=5)], reverb=.45),
    patch('D-50 · Pizzicato Strings', D50, 'plucked attack transient on a harpsichord-like partial', [
        layer(synthesisType=4, wavetableIdx=41, laMix=.25, sawLevel=.7, transientType=2, transientLevel=1.0,
              transientDecay=.15, transientTone=.6, attackSec=.001, decaySec=.35, sustainLevel=0.0, releaseSec=.3,
              cutoffHz=5200, volume=.7),
        layer(synthesisType=4, wavetableIdx=15, laMix=.0, octave=1, attackSec=.001, decaySec=.25, sustainLevel=0.0,
              releaseSec=.25, volume=.3)],
          reverb=.3),
    patch('D-50 · Soundtrack Pad', D50, 'slow evolving cinematic pad with a bowed attack', [
        layer(synthesisType=4, wavetableIdx=34, wavetableMorph=.4, laMix=.4, sawLevel=.7, transientType=6,
              transientLevel=.5, transientDecay=.6, transientTone=.4, filterType=5, cutoffHz=1500, resonanceQ=1.5,
              filterEnvAmt=.4, filtAttackSec=2.0, filtDecaySec=3.0, filtSustainLvl=.6, attackSec=1.5, decaySec=3.0,
              sustainLevel=.85, releaseSec=3.0, volume=.75, analog=.2, fx={CHORUS_I: {'mix': .35}})],
          lfos=[lfo(waveType=5, rateHz=.3, depth=.7, destCutoff=600)], reverb=.5),
    patch('D-50 · Nylon Atmosphere', D50, 'nylon-style pluck over an airy pad', [
        layer(synthesisType=4, wavetableIdx=33, laMix=.15, transientType=2, transientLevel=.9, transientDecay=.3,
              transientTone=.45, attackSec=.001, decaySec=1.8, sustainLevel=0.0, releaseSec=.8, cutoffHz=4200,
              volume=.65),
        layer(synthesisType=4, wavetableIdx=34, laMix=.2, attackSec=1.2, sustainLevel=.7, releaseSec=3.0,
              cutoffHz=2800, volume=.35, fx={CHORUS_II: {'mix': .45}})],
          reverb=.45),
]

# Yamaha DX7 style: FM4 with per-operator envelopes. Amp contour stays open; operators shape the sound.
BANK += [
    patch('DX7 · E.Piano Tines', DX7, 'two FM stacks: warm body plus a bright tine', [
        fm4(3, [1, 1, 1, 14], [.9, .55, .5, .35], [.001] * 4, [2.6, 1.4, 1.2, .12], [0, .05, 0, 0],
            fm4VelMod=.8, releaseSec=.5, volume=.75, fx={CHORUS_I: {'mix': .3, 'rateHz': .6}})],
          reverb=.15, hall=False),
    patch('DX7 · Mellow Rhodes', DX7, 'soft FM electric piano with phaser', [
        fm4(3, [1, 1, 1, 3], [.9, .35, .4, .2], [.001] * 4, [3.0, .9, 2.0, .3], [0, .1, 0, 0],
            fm4VelMod=.7, releaseSec=.6, volume=.75, fx={PHASER_I: {'mix': .3, 'rateHz': .4}})],
          reverb=.15, hall=False),
    patch('DX7 · Solid Bass', DX7, 'punchy FM bass with feedback edge', [
        fm4(1, [1, 1, .5, 3], [1, .6, .3, .25], [.001] * 4, [1.2, .35, .2, .08], [.3, .15, 0, 0],
            fm4Feedback=.3, fm4VelMod=.6, octave=-1, releaseSec=.12, volume=.8)],
          mono=(False, 0)),
    patch('DX7 · Brass Fanfare', DX7, 'FM brass with swelling modulators', [
        fm4(2, [1, 1, 1, 1], [1, .55, .4, .3], [.05, .08, .1, .06], [1, .6, .5, .8], [.8, .45, .35, .4],
            fm4Feedback=.4, fm4VelMod=.5, releaseSec=.35, volume=.7, wheelVibratoCents=25)],
          reverb=.2),
    patch('DX7 · Tubular Bells', DX7, 'inharmonic FM bell partials', [
        fm4(3, [1, 3.5, 2, 7.07], [.8, .5, .5, .35], [.001] * 4, [4, 2.5, 3.5, 1.2], [0, 0, 0, 0],
            fm4VelMod=.6, releaseSec=2.5, volume=.75)],
          reverb=.35),
    patch('DX7 · Marimba', DX7, 'woody FM mallet with a short metallic strike', [
        fm4(3, [1, 4, 1, 10], [.9, .5, .4, .3], [.001] * 4, [.6, .08, .4, .04], [0, 0, 0, 0],
            fm4VelMod=.8, releaseSec=.3, volume=.8)],
          reverb=.2, hall=False),
    patch('DX7 · Clav', DX7, 'bright FM clavinet stack', [
        fm4(0, [1, 3, 1, 5], [.9, .55, .3, .2], [.001] * 4, [.9, .5, .3, .1], [.2, .25, 0, 0],
            fm4Feedback=.2, fm4VelMod=.7, releaseSec=.1, volume=.75, fx={PHASER_I: {'mix': .2}})]),
    patch('DX7 · Glass Strings', DX7, 'one modulator feeding three detuned carriers', [
        fm4(4, [1, 2, 1.003, 1], [.8, .45, .6, .25], [.5, .6, .7, .8], [2, 2, 2, 3], [.9, .8, .9, .5],
            fm4VelMod=.3, releaseSec=1.5, volume=.7, fx={CHORUS_II: {'mix': .4}})],
          reverb=.3),
]

# Roland Jupiter-8 style: dual VCO, 24 dB OTA filter, unison, sync, cross-mod.
BANK += [
    patch('Jupiter-8 · Poly Brass', JP8, 'two detuned saws through an enveloped 24 dB filter', [
        layer(waveType=9, sawLevel=1, osc2Level=.9, osc2Detune=7, cutoffHz=700, resonanceQ=1.0, filterEnvAmt=.6,
              filtAttackSec=.08, filtDecaySec=.7, filtSustainLvl=.45, attackSec=.04, decaySec=1.0, sustainLevel=.9,
              releaseSec=.4, analog=.35, volume=.7)],
          reverb=.15),
    patch('Jupiter-8 · Sweeping Strings', JP8, 'PWM and saw strings with an octave layer', [
        layer(waveType=9, sawLevel=0, pulseLevel=.8, pwmDepth=.2, pwmRateHz=.5, osc2Level=.7, osc2Detune=9,
              cutoffHz=2600, attackSec=.6, decaySec=1.5, sustainLevel=.9, releaseSec=1.8, analog=.35, volume=.6,
              fx={PHASER_I: {'mix': .25, 'rateHz': .2}}),
        layer(waveType=9, sawLevel=.8, osc2Level=.6, osc2Detune=-6, octave=1, cutoffHz=3200, attackSec=.8,
              sustainLevel=.85, releaseSec=2.0, analog=.35, volume=.32)],
          reverb=.3),
    patch('Jupiter-8 · Unison Lead', JP8, 'four-voice unison saw lead with glide', [
        layer(waveType=9, sawLevel=1, osc2Level=.9, osc2Detune=12, unisonVoices=4, unisonDetune=14,
              unisonSpread=.6, cutoffHz=2800, resonanceQ=2.0, filterEnvAmt=.4, filtDecaySec=.6, filtSustainLvl=.5,
              attackSec=.005, releaseSec=.2, analog=.3, volume=.6, fx={ECHO: {'mix': .2, 'syncEnabled': True, 'syncDivision': 8}})],
          mono=(True, .08)),
    patch('Jupiter-8 · Sync Lead', JP8, 'hard-synced oscillator swept by the filter envelope', [
        layer(waveType=32, osc2Level=.7, osc2Semitones=7, filterEnvAmt=.3, filtDecaySec=.6, filtSustainLvl=.2,
              cutoffHz=4500, resonanceQ=1.5, attackSec=.003, releaseSec=.2, analog=.3, volume=.6)],
          mono=(True, .06), delay=(330, .3, .15)),
    patch('Jupiter-8 · Fat Bass', JP8, 'saw plus sub-octave pulse, punchy filter', [
        layer(waveType=9, sawLevel=.9, osc2Level=.8, osc2Wave=1, osc2Semitones=-12, subLevel=.5, cutoffHz=450,
              resonanceQ=2.2, filterEnvAmt=.55, filtDecaySec=.3, filtSustainLvl=.2, attackSec=.002, decaySec=.5,
              sustainLevel=.6, releaseSec=.12, analog=.3, volume=.75)],
          mono=(False, 0)),
    patch('Jupiter-8 · Cross-Mod Bells', JP8, 'ring-modulated oscillators for metallic bells', [
        layer(waveType=12, osc2Semitones=7, cutoffHz=9000, filterType=0, attackSec=.002, decaySec=2.2,
              sustainLevel=0.0, releaseSec=1.6, analog=.2, volume=.65)],
          reverb=.3),
    patch('Jupiter-8 · Hollow Pad', JP8, 'narrow pulse and octave triangle, high-passed', [
        layer(waveType=9, sawLevel=0, pulseLevel=.8, pulseWidth=.25, osc2Level=.6, osc2Wave=2, osc2Semitones=12,
              cutoffHz=1500, resonanceQ=1.2, highpassHz=200, attackSec=1.2, decaySec=2.0, sustainLevel=.85,
              releaseSec=2.5, analog=.35, volume=.75, fx={PHASER_II: {'mix': .3, 'rateHz': .15}})],
          reverb=.35),
    patch('Jupiter-8 · Arp Stab', JP8, 'short resonant stab with synced echo', [
        layer(waveType=9, sawLevel=1, osc2Level=.8, osc2Detune=6, cutoffHz=1200, resonanceQ=2.5, filterEnvAmt=.7,
              filtDecaySec=.15, filtSustainLvl=.05, attackSec=.001, decaySec=.3, sustainLevel=0.0, releaseSec=.25,
              analog=.3, volume=.75, fx={ECHO: {'mix': .25, 'feedback': .35, 'syncEnabled': True, 'syncDivision': 8}})]),
]

# Roland Juno-60 style: single DCO (saw/pulse/sub/noise), 24 dB OTA filter, chorus I/II.
BANK += [
    patch('Juno-60 · Chorus Strings', JUNO, 'PWM and saw through chorus II', [
        layer(waveType=8, sawLevel=.7, pulseLevel=.5, pwmDepth=.18, pwmRateHz=.7, cutoffHz=3200, resonanceQ=.9,
              filterEnvAmt=.15, highpassHz=150, attackSec=.35, decaySec=1.0, sustainLevel=.9, releaseSec=1.2,
              analog=.25, volume=.7, fx={CHORUS_II: {'mix': .5, 'rateHz': .83, 'depth': .6}})],
          reverb=.2),
    patch('Juno-60 · Brass Section', JUNO, 'saw and sub with a quick filter swell', [
        layer(waveType=8, sawLevel=1, subLevel=.3, cutoffHz=900, resonanceQ=1.0, filterEnvAmt=.55, filtAttackSec=.06,
              filtDecaySec=.5, filtSustainLvl=.4, attackSec=.03, decaySec=.8, sustainLevel=.85, releaseSec=.35,
              analog=.25, volume=.7, fx={CHORUS_I: {'mix': .35}})]),
    patch('Juno-60 · Sub Bass', JUNO, 'pulse, saw and sub-octave square', [
        layer(waveType=8, sawLevel=.3, pulseLevel=.6, subLevel=.9, octave=-1, cutoffHz=600, resonanceQ=2.5,
              filterEnvAmt=.45, filtDecaySec=.25, filtSustainLvl=.1, filterKeyTrack=.3, attackSec=.002, decaySec=.4,
              sustainLevel=.7, releaseSec=.15, analog=.2, volume=.8)],
          mono=(False, 0)),
    patch('Juno-60 · Warm Pad', JUNO, 'slow pad through chorus I+II', [
        layer(waveType=8, sawLevel=.8, pulseLevel=.5, pwmDepth=.25, pwmRateHz=.5, subLevel=.25, cutoffHz=1800,
              resonanceQ=1.3, filterEnvAmt=.1, filtAttackSec=1.2, filtDecaySec=2.0, filtSustainLvl=.7, attackSec=.9,
              decaySec=2.0, sustainLevel=.9, releaseSec=2.2, analog=.3, volume=.75,
              fx={CHORUS_I: {'mix': .3}, CHORUS_II: {'mix': .3}})],
          reverb=.3),
    patch('Juno-60 · Pluck Keys', JUNO, 'narrow pulse pluck with resonant envelope', [
        layer(waveType=8, sawLevel=.4, pulseLevel=.8, pulseWidth=.35, cutoffHz=700, resonanceQ=3.0, filterEnvAmt=.6,
              filtDecaySec=.22, filtSustainLvl=0.0, attackSec=.002, decaySec=.6, sustainLevel=0.0, releaseSec=.35,
              analog=.25, volume=.8, fx={CHORUS_I: {'mix': .35}})]),
    patch('Juno-60 · PWM Sweep', JUNO, 'slow PWM with an LFO-swept resonant filter', [
        layer(waveType=8, sawLevel=0, pulseLevel=1, pwmDepth=.35, pwmRateHz=.25, cutoffHz=900, resonanceQ=4.0,
              attackSec=.6, decaySec=2.0, sustainLevel=.9, releaseSec=2.0, analog=.25, volume=.75,
              fx={CHORUS_II: {'mix': .45}})],
          lfos=[lfo(waveType=1, rateHz=.12, depth=.8, destCutoff=1800)], reverb=.3),
    patch('Juno-60 · Organ Sub', JUNO, 'square and sub-octave organ', [
        layer(waveType=8, sawLevel=0, pulseLevel=.8, subLevel=.7, cutoffHz=5200, resonanceQ=.7, attackSec=.003,
              decaySec=.2, sustainLevel=1.0, releaseSec=.08, analog=.2, volume=.7, fx={CHORUS_I: {'mix': .3}})]),
    patch('Juno-60 · Resonant Arp', JUNO, 'resonant saw driven by the step sequencer', [
        layer(waveType=8, sawLevel=1, subLevel=.4, cutoffHz=500, resonanceQ=5.5, filterEnvAmt=.7, filtDecaySec=.18,
              filtSustainLvl=0.0, attackSec=.001, decaySec=.25, sustainLevel=.2, releaseSec=.2, analog=.25,
              seqEnabled=True, seqBpm=120, seqSteps=8, seqGate=.5, seqNotes=[0, 12, 7, 3, 0, 12, 10, 7] + [0] * 8,
              volume=.8, fx={ECHO: {'mix': .22, 'feedback': .3, 'syncEnabled': True, 'syncDivision': 8},
                             CHORUS_I: {'mix': .3}})]),
]

# Sequential Prophet-5 style: two VCOs, CEM-type 24 dB filter, poly-mod, strong analog drift.
BANK += [
    patch('Prophet-5 · Classic Brass', P5, 'detuned saws with a slow filter contour', [
        layer(waveType=9, sawLevel=1, osc2Level=.8, osc2Detune=5, cutoffHz=650, resonanceQ=1.1, filterEnvAmt=.65,
              filtAttackSec=.1, filtDecaySec=.9, filtSustainLvl=.5, attackSec=.06, decaySec=1.0, sustainLevel=.85,
              releaseSec=.45, analog=.45, volume=.7)],
          reverb=.15),
    patch('Prophet-5 · Sync Sweep Lead', P5, 'poly-mod sync sweep from the filter envelope', [
        layer(waveType=32, osc2Level=.9, filterEnvAmt=.2, filtAttackSec=.01, filtDecaySec=1.4, filtSustainLvl=.1,
              cutoffHz=5000, resonanceQ=1.2, attackSec=.003, releaseSec=.25, analog=.4, volume=.6)],
          mono=(True, .05), delay=(300, .3, .18)),
    patch('Prophet-5 · Poly Strings', P5, 'saw ensemble with gentle phaser', [
        layer(waveType=9, sawLevel=.8, pulseLevel=.2, osc2Level=.7, osc2Detune=8, cutoffHz=2200, attackSec=.45,
              decaySec=1.5, sustainLevel=.9, releaseSec=1.5, analog=.5, volume=.7, wheelVibratoCents=18,
              fx={PHASER_I: {'mix': .2, 'rateHz': .25}})],
          reverb=.25),
    patch('Prophet-5 · Poly-Mod Bell', P5, 'oscillator FM for a glassy bell', [
        layer(synthesisType=1, fmRatio=3.5, fmIndex=2.5, cutoffHz=6000, attackSec=.002, decaySec=2.0,
              sustainLevel=0.0, releaseSec=1.5, analog=.35, volume=.7)],
          reverb=.35),
    patch('Prophet-5 · Clav Pluck', P5, 'narrow pulse pluck with a key click', [
        layer(waveType=1, pulseWidth=.2, cutoffHz=1500, resonanceQ=1.5, filterEnvAmt=.5, filtDecaySec=.18,
              filtSustainLvl=0.0, attackSec=.001, decaySec=.5, sustainLevel=.05, releaseSec=.15, transientType=4,
              transientLevel=.3, analog=.4, volume=.8)]),
    patch('Prophet-5 · Analog Bass', P5, 'two-voice unison saw and pulse bass', [
        layer(waveType=9, sawLevel=.9, osc2Level=.9, osc2Wave=1, osc2Semitones=-12, unisonVoices=2, unisonDetune=6,
              cutoffHz=380, resonanceQ=2.0, filterEnvAmt=.5, filtDecaySec=.25, filtSustainLvl=.15, attackSec=.002,
              decaySec=.5, sustainLevel=.7, releaseSec=.12, analog=.4, volume=.75)],
          mono=(False, 0)),
    patch('Prophet-5 · Warm Pad', P5, 'slow PWM pad with heavy analog drift', [
        layer(waveType=9, sawLevel=.6, pulseLevel=.7, pwmDepth=.15, osc2Level=.5, osc2Wave=2, osc2Semitones=12,
              cutoffHz=1300, resonanceQ=1.6, filterEnvAmt=.25, filtAttackSec=1.5, filtDecaySec=2.0, filtSustainLvl=.6,
              attackSec=1.2, decaySec=2.0, sustainLevel=.9, releaseSec=2.8, analog=.6, volume=.75)],
          reverb=.3),
    patch('Prophet-5 · Sample & Hold Sweep', P5, 'random-stepped resonant filter', [
        layer(waveType=9, sawLevel=1, osc2Level=.6, osc2Detune=15, cutoffHz=1000, resonanceQ=7.0, attackSec=.02,
              decaySec=1.0, sustainLevel=.9, releaseSec=.8, analog=.45, volume=.65,
              fx={ECHO: {'mix': .3, 'feedback': .4, 'syncEnabled': True, 'syncDivision': 8}})],
          lfos=[lfo(waveType=4, rateHz=7.0, depth=.9, destCutoff=2500)]),
]

# Moog Minimoog style: three VCOs, 24 dB ladder, monophonic with glide.
BANK += [
    patch('Minimoog · Classic Bass', MINI, 'three VCOs and a punchy ladder envelope', [
        layer(waveType=31, sawLevel=.9, osc2Level=.8, osc2Detune=6, subLevel=.6, octave=-1, filterType=4,
              cutoffHz=350, resonanceQ=2.5, filterEnvAmt=.55, filtAttackSec=.001, filtDecaySec=.35, filtSustainLvl=.3,
              attackSec=.002, decaySec=.5, sustainLevel=.85, releaseSec=.08, analog=.4, volume=.8)],
          mono=(True, 0)),
    patch('Minimoog · Gliding Lead', MINI, 'square-ish lead with long portamento', [
        layer(waveType=31, sawLevel=.6, osc2Level=.9, osc2Wave=1, osc2Detune=4, subLevel=.5, filterType=4,
              cutoffHz=1200, resonanceQ=2.8, filterEnvAmt=.45, filtDecaySec=.7, filtSustainLvl=.5, attackSec=.01,
              releaseSec=.25, analog=.4, wheelVibratoCents=25, volume=.65)],
          mono=(True, .12), reverb=.15),
    patch('Minimoog · Fat Saw Lead', MINI, 'detuned saws with drive into the ladder', [
        layer(waveType=31, sawLevel=.9, osc2Level=.9, osc2Detune=9, subLevel=.7, filterType=4, cutoffHz=2200,
              resonanceQ=1.5, filterEnvAmt=.5, filtDecaySec=.4, filtSustainLvl=.6, drive=.3, attackSec=.005,
              releaseSec=.2, analog=.4, volume=.6)],
          mono=(True, .05), delay=(320, .25, .12)),
    patch('Minimoog · Funk Bass', MINI, 'snappy resonant envelope bass', [
        layer(waveType=31, sawLevel=.9, subLevel=.4, octave=-1, filterType=4, cutoffHz=260, resonanceQ=5.0,
              filterEnvAmt=.75, filtDecaySec=.16, filtSustainLvl=0.0, velToCutoff=.4, attackSec=.001, decaySec=.25,
              sustainLevel=.5, releaseSec=.06, analog=.35, volume=.85)],
          mono=(True, 0)),
    patch('Minimoog · Flute Lead', MINI, 'triangle lead with breath noise and delayed vibrato', [
        layer(waveType=31, sawLevel=.1, osc2Level=.9, osc2Wave=2, noiseLevel=.06, filterType=4, cutoffHz=1800,
              resonanceQ=.9, attackSec=.06, releaseSec=.25, analog=.3, volume=.75)],
          lfos=[lfo(rateHz=5.0, depth=1.0, destPitch=12, delaySec=.4, fadeSec=.5, keySync=True)],
          mono=(True, .04), reverb=.2),
    patch('Minimoog · Sub Bass', MINI, 'triangle and saw sub bass, nearly closed filter', [
        layer(waveType=31, sawLevel=.4, osc2Level=.8, osc2Wave=2, subLevel=.6, octave=-1, filterType=4,
              cutoffHz=220, resonanceQ=1.0, filterEnvAmt=.25, filtDecaySec=.3, attackSec=.002, sustainLevel=1.0,
              releaseSec=.1, analog=.3, volume=.9)],
          mono=(True, 0)),
    patch('Minimoog · Brass Lead', MINI, 'swelling filter brass lead', [
        layer(waveType=31, sawLevel=.9, osc2Level=.8, osc2Detune=7, subLevel=.3, filterType=4, cutoffHz=600,
              resonanceQ=1.5, filterEnvAmt=.7, filtAttackSec=.09, filtDecaySec=.5, filtSustainLvl=.5,
              attackSec=.03, sustainLevel=.9, releaseSec=.2, analog=.4, volume=.65)],
          mono=(True, .03)),
    patch('Minimoog · Wah Resonance', MINI, 'high-resonance ladder with an enveloped wah', [
        layer(waveType=31, sawLevel=1, osc2Level=.7, subLevel=.5, filterType=4, cutoffHz=300, resonanceQ=7.5,
              filterEnvAmt=.8, filtAttackSec=.12, filtDecaySec=.6, filtSustainLvl=.2, attackSec=.01, releaseSec=.2,
              analog=.4, volume=.6)],
          mono=(True, .08)),
]

assert len(BANK) == 48


def main():
    gains = {}
    if '--gains' in sys.argv:
        gains = {int(k): float(v) for k, v in json.loads(Path(sys.argv[sys.argv.index('--gains') + 1]).read_text()).items()}
    catalog = []
    for offset, entry in enumerate(BANK):
        pid = FIRST_ID + offset
        params = copy.deepcopy(entry['params'])
        params['synthPresetId'] = pid
        # Level-match: master volume first (max 1.2 leaves headroom on the knob), then layer volumes (max 1.5).
        target = params['synthMasterVol'] * gains.get(pid, 1.0)
        params['synthMasterVol'] = round(min(1.2, max(.2, target)), 3)
        rest = target / params['synthMasterVol']
        if abs(rest - 1.0) > 1e-3:
            for lay in params['layers']:
                if lay['enabled']:
                    lay['volume'] = round(min(1.5, lay['volume'] * rest), 3)
        doc = {'factoryId': pid, 'name': entry['name'], 'category': entry['category'],
               'inspiration': entry['inspiration'], 'originalDesign': True, 'params': params}
        (FACTORY / f'{pid:03d}.json').write_text(json.dumps(doc, indent=1, ensure_ascii=False), encoding='utf8')
        catalog.append({'id': pid, 'name': entry['name'], 'category': entry['category']})

    text = CATALOG.read_text(encoding='utf8')
    m = re.search(r'export const SYNTH_FACTORY: readonly FactoryPatch\[\] = (\[.*?\]);\n', text, re.S)
    existing = [p for p in json.loads(m.group(1)) if p['id'] < FIRST_ID]
    text = text.replace(m.group(1), json.dumps(existing + catalog, ensure_ascii=False, separators=(',', ':')))
    m = re.search(r'export const SYNTH_FACTORY_CATEGORIES: readonly string\[\] = (\[.*?\]);', text)
    cats = [c for c in json.loads(m.group(1)) if c not in {D50, DX7, JP8, JUNO, P5, MINI}] + [D50, DX7, JP8, JUNO, P5, MINI]
    text = text.replace(m.group(1), json.dumps(cats, ensure_ascii=False))
    text = re.sub(r'^// Generated from .*$', '// 384 generated factory patches + 48 vintage-inspired designs (tools/build_vintage_presets.py).',
                  text, count=1, flags=re.M)
    CATALOG.write_text(text, encoding='utf8')
    print(f'wrote {len(BANK)} presets ({FIRST_ID}-{FIRST_ID + len(BANK) - 1}) and updated the catalogue')


if __name__ == '__main__':
    main()
