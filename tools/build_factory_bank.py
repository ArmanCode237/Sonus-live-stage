"""Factory synth bank v32.3: quality over quantity.

  1-96    new bank: 12 categories x 8 hand-designed patches inspired by legendary
          instruments (TB-303, Minimoog, OB-X, CS-80, Prophet, ARP, MS-20, SH-101,
          Mellotron, Solina, Synclavier, PPG, Polymoog...). They use the v32.3
          features: modulation matrix (wheel / aftertouch / velocity / macros),
          303-style sequences with accent and slide.
  97-184  kept: the 48 vintage designs, 24 DX7 voices and 16 LA32 designs
          (previously 385-472), renumbered.

    python tools/build_factory_bank.py [--gains gains.json]

Deletes presets/synth/factory/*.json, writes 1-184 and the catalogue in
webui/src/data/synthFactory.ts. Original parameter designs: no ROM, sample or
third-party patch data is used (the DX7 voices keep their cartridge origin).
"""
from __future__ import annotations
import copy, json, re, sys
from pathlib import Path

import build_vintage_presets as vp

ROOT = Path(__file__).resolve().parents[1]
FACTORY = ROOT / 'presets/synth/factory'
CATALOG = ROOT / 'webui/src/data/synthFactory.ts'
KEPT_FIRST, KEPT_LAST = 385, 472           # source ids of the kept presets (before renumbering)
CHORUS_I, CHORUS_II, PHASER_I, PHASER_II, ECHO = range(5)
WHEEL, BEND, AT, VEL, KEY, AENV, FENV, LFO1, LFO2, LFO3, LFO4, RND, M1, M2, M3, M4 = range(16)
SAW, PULSE, TRI, SINE, SUPERSAW = 0, 1, 2, 3, 4
SYNC, JUNO, JUPITER, SUPERPULSE, VOX, HARMONIC, PWM_ENS, OCT_FIFTH, REESE, TRI_ENS = 7, 8, 9, 11, 13, 15, 21, 23, 24, 28
PD_RESO, TRIPLE, SYNC_SWEEP, HYPERSAW, ORGAN = 30, 31, 32, 33, 18
LP12, HP12, BP12, LADDER, OTA = 0, 1, 2, 4, 5

SEQ_KEYS = ('seqAccent', 'seqSlide', 'seqDivision', 'seqSync')


def L(**kw):
    """Layer with the v32.3 sequencer extras (accent / slide / division / sync)."""
    extra = {k: kw.pop(k) for k in list(kw) if k in SEQ_KEYS}
    out = vp.layer(**kw)
    out['seqAccent'] = [0] * 16
    out['seqSlide'] = [0] * 16
    out['seqDivision'] = 3
    out['seqSync'] = False
    out.update(extra)
    return out


def mod(source, target, amount, bipolar=False, curve=0):
    return {'source': source, 'target': target, 'amount': amount, 'bipolar': bipolar, 'curve': curve}


def P(name, cat, insp, layers, mods=(), macros=None, clock=False, **kw):
    doc = vp.patch(name, cat, insp, layers, **kw)
    doc['params']['modMatrix'] = list(mods)
    doc['params']['synthMacros'] = list(macros or [0, 0, 0, 0])
    doc['params']['synthSeqGlobalClock'] = clock
    doc['params']['synthSeqSharePattern'] = False
    return doc


def lfo(**kw):
    return vp.lfo(**kw)


def seq(notes, accent=(), slide=(), steps=16, gate=.55, division=3):
    """Sequencer fields for L(): notes are semitones (None = rest)."""
    n = [99 if x is None else x for x in notes] + [0] * (16 - len(notes))
    return dict(seqEnabled=True, seqNotes=n[:16], seqSteps=steps, seqGate=gate, seqSync=True, seqDivision=division,
                seqAccent=[1 if i in accent else 0 for i in range(16)], seqSlide=[1 if i in slide else 0 for i in range(16)])


WARM_CHORUS = {CHORUS_I: {'mix': .4, 'rateHz': .513, 'depth': .75}}
WIDE_CHORUS = {CHORUS_II: {'mix': .45, 'rateHz': .863, 'depth': .8}}
BRIGHT_WHEEL = lambda l=1, a=.3: mod(WHEEL, f'L{l}_cut', a)                       # wheel opens the filter
VEL_CUT = lambda l=1, a=.25: mod(VEL, f'L{l}_cut', a)                              # harder = brighter (per voice)
AT_VIB = lambda l=1: mod(AT, f'L{l}_wheelVibrato', .35)                            # pressure adds vibrato

BANK = []
# ------------------------------------------------------------------ BASS
BASS = 'BASS'
BANK += [
    P('TB-303 · Acid Line', BASS, 'saw, resonant 18 dB-style ladder, accent and slide sequence', [
        L(sawLevel=1, filterType=LADDER, cutoffHz=340, resonanceQ=8.5, filterEnvAmt=.62, filtDecaySec=.18, filtSustainLvl=0,
          attackSec=.001, decaySec=.4, sustainLevel=.7, releaseSec=.05, octave=-1, volume=.75, drive=.25, analog=.1,
          **seq([0, 0, 12, 0, 3, 0, 15, 12, 0, 7, 0, 10, 0, 12, 5, 0], accent=(2, 6, 11, 14), slide=(1, 5, 9, 13), gate=.5))],
      mods=[mod(M1, 'L1_cut', .45), mod(M2, 'L1_res', .3), BRIGHT_WHEEL(1, .2)], macros=[.2, .1, 0, 0], mono=(False, 0), clock=True,
      delay=(375, .3, .12)),
    P('TB-303 · Squelch Square', BASS, 'square 303, deep envelope sweep, slides', [
        L(sawLevel=0, pulseLevel=1, pulseWidth=.5, filterType=LADDER, cutoffHz=260, resonanceQ=9.5, filterEnvAmt=.7,
          filtDecaySec=.25, filtSustainLvl=0, attackSec=.001, sustainLevel=.8, releaseSec=.05, octave=-1, volume=.75, drive=.3,
          **seq([0, 12, 0, 0, 10, 0, 7, 0, 0, 12, 3, 0, 5, 0, 12, 10], accent=(1, 9, 15), slide=(3, 8, 14), gate=.45))],
      mods=[mod(M1, 'L1_cut', .5), BRIGHT_WHEEL(1, .25)], clock=True),
    P('Minimoog · Deep Bass', BASS, 'three VCOs, ladder at low cutoff, punchy contour', [
        L(waveType=TRIPLE, sawLevel=.9, osc2Level=.8, osc2Detune=6, subLevel=.7, octave=-1, filterType=LADDER, cutoffHz=280,
          resonanceQ=2.0, filterEnvAmt=.45, filtDecaySec=.35, filtSustainLvl=.2, velToCutoff=.3, attackSec=.002, decaySec=.6,
          sustainLevel=.8, releaseSec=.08, analog=.35, volume=.85, drive=.15)],
      mods=[BRIGHT_WHEEL(1, .25), VEL_CUT(1, .2)], mono=(True, .02)),
    P('Moog Taurus · Pedal Bass', BASS, 'huge sawtooth pedal bass with a slow release', [
        L(sawLevel=.9, subLevel=.8, octave=-2, filterType=LADDER, cutoffHz=520, resonanceQ=1.2, filterEnvAmt=.3, filtDecaySec=.8,
          filtSustainLvl=.5, attackSec=.01, sustainLevel=1, releaseSec=.9, analog=.3, volume=.85)],
      mods=[BRIGHT_WHEEL(1, .2)], mono=(True, .06)),
    P('SH-101 · Rubber Bass', BASS, 'pulse + sub, snappy filter, a little resonance', [
        L(waveType=PULSE, sawLevel=0, pulseLevel=.9, pulseWidth=.4, subLevel=.6, octave=-1, filterType=OTA, cutoffHz=420,
          resonanceQ=3.0, filterEnvAmt=.55, filtDecaySec=.2, filtSustainLvl=.15, attackSec=.001, sustainLevel=.75, releaseSec=.06,
          velToCutoff=.35, volume=.8)],
      mods=[VEL_CUT(1, .2), mod(M1, 'L1_pw', .3)], macros=[0, 0, 0, 0], mono=(True, 0)),
    P('Juno · Sub Bass', BASS, 'DCO pulse and sub oscillator, round and clean', [
        L(waveType=JUNO, sawLevel=.4, pulseLevel=.5, subLevel=.8, octave=-1, filterType=OTA, cutoffHz=380, resonanceQ=.8,
          filterEnvAmt=.2, filtDecaySec=.4, attackSec=.003, sustainLevel=.95, releaseSec=.1, volume=.85)],
      mods=[BRIGHT_WHEEL(1, .2)]),
    P('Synclavier · FM Slap Bass', BASS, 'two-operator slap with a bright attack', [
        vp.fm4(0, [1.0, 1.0, 3.0, 1.0], [1.0, .65, .25, .1], [.001, .001, .001, .001], [.9, .25, .12, .1], [.6, .1, 0, 0],
               octave=-1, velToAmp=1.0, releaseSec=.08, volume=.8)],
      mods=[mod(VEL, 'L1_cut', .2)], mono=(True, 0)),
    P('Reese · Detuned Bass', BASS, 'two detuned saws beating, dark and wide', [
        L(waveType=REESE, sawLevel=1, octave=-1, filterType=LADDER, cutoffHz=600, resonanceQ=1.5, attackSec=.005,
          sustainLevel=1, releaseSec=.15, unisonVoices=2, unisonDetune=9, unisonSpread=.6, volume=.8, drive=.2)],
      mods=[BRIGHT_WHEEL(1, .35), mod(LFO1, 'L1_cut', .12, True)], lfos=[lfo(rateHz=.3, depth=1.0)], mono=(True, .04)),
]

# ------------------------------------------------------------------ LEADS
LEADS = 'LEADS'
BANK += [
    P('Minimoog · Classic Lead', LEADS, 'three VCOs into the ladder, glide, wheel vibrato', [
        L(waveType=TRIPLE, sawLevel=.9, osc2Level=.85, osc2Detune=7, subLevel=.3, filterType=LADDER, cutoffHz=1800,
          resonanceQ=2.2, filterEnvAmt=.4, filtDecaySec=.5, filtSustainLvl=.55, attackSec=.005, sustainLevel=1, releaseSec=.2,
          analog=.4, volume=.65, wheelMode=0, wheelVibratoCents=30)],
      mods=[AT_VIB(1), mod(AT, 'L1_cut', .15)], mono=(True, .06), delay=(330, .25, .14)),
    P('OB-X · Sync Lead', LEADS, 'hard-sync sweep from the filter envelope, wheel sweeps the sync', [
        L(waveType=SYNC_SWEEP, sawLevel=1, osc2Level=.6, filterType=OTA, cutoffHz=4200, resonanceQ=1.2, filterEnvAmt=.35,
          filtDecaySec=.6, filtSustainLvl=.4, attackSec=.003, sustainLevel=1, releaseSec=.25, volume=.6, analog=.2)],
      mods=[mod(WHEEL, 'L1_osc2', .4), mod(AT, 'L1_cut', .2)], mono=(True, .03), delay=(375, .3, .16)),
    P('Prophet-5 · Poly-Mod Lead', LEADS, 'filter envelope into OSC A pitch: the classic metallic lead', [
        L(sawLevel=1, osc2Level=.7, osc2Semitones=7, fmIndex=1.2, fmRatio=1.5, filterType=OTA, cutoffHz=2600, resonanceQ=2.5,
          filterEnvAmt=.5, filtDecaySec=.45, filtSustainLvl=.35, attackSec=.004, sustainLevel=.9, releaseSec=.3, volume=.6)],
      mods=[mod(FENV, 'L1_semi', .04), BRIGHT_WHEEL(1, .2)], mono=(True, .02)),
    P('ARP Odyssey · Duo Lead', LEADS, 'pulse and saw, bright, slightly nasal', [
        L(sawLevel=.8, pulseLevel=.6, pulseWidth=.3, filterType=OTA, cutoffHz=3000, resonanceQ=3.0, filterEnvAmt=.3,
          filtDecaySec=.35, filtSustainLvl=.5, attackSec=.003, sustainLevel=1, releaseSec=.18, volume=.6, drive=.2)],
      mods=[mod(WHEEL, 'L1_pw', .3), AT_VIB(1)], mono=(True, .04)),
    P('CS-80 · Expressive Lead', LEADS, 'aftertouch brightness and vibrato, like the CS-80 ribbon and pressure', [
        L(sawLevel=1, pulseLevel=.3, filterType=OTA, cutoffHz=1300, resonanceQ=1.8, attackSec=.06, sustainLevel=1,
          releaseSec=.4, volume=.65, analog=.3, fx=WARM_CHORUS)],
      mods=[mod(AT, 'L1_cut', .35), mod(AT, 'L1_wheelVibrato', .45), mod(BEND, 'L1_semi', .04, True)], mono=(True, .08),
      reverb=.2),
    P('Lucky Man · Portamento Square', LEADS, 'square wave lead with a long glide', [
        L(waveType=PULSE, sawLevel=0, pulseLevel=1, pulseWidth=.5, filterType=LADDER, cutoffHz=2400, resonanceQ=1.4,
          attackSec=.01, sustainLevel=1, releaseSec=.3, volume=.6, analog=.3)],
      mods=[BRIGHT_WHEEL(1, .2)], mono=(True, .18), delay=(420, .3, .15)),
    P('Synclavier · FM Lead', LEADS, 'bright FM lead, velocity brightens the modulator', [
        vp.fm4(1, [1.0, 2.0, 3.0, 1.0], [1.0, .55, .25, .15], [.003, .003, .003, .003], [2.0, 1.0, .8, .6], [.8, .5, .3, .2],
               releaseSec=.25, volume=.6)],
      mods=[AT_VIB(1)], mono=(True, .03), reverb=.15),
    P('MS-20 · Screamer', LEADS, 'driven saw with an aggressive resonant filter', [
        L(sawLevel=1, filterType=LP12, cutoffHz=1500, resonanceQ=7.5, filterEnvAmt=.45, filtDecaySec=.3, filtSustainLvl=.4,
          attackSec=.002, sustainLevel=1, releaseSec=.15, drive=.6, highpassHz=180, volume=.55)],
      mods=[mod(WHEEL, 'L1_cut', .35), mod(M1, 'L1_drive', .4)], mono=(True, .03)),
]

# ------------------------------------------------------------------ PADS
PADS = 'PADS'
BANK += [
    P('CS-80 · Blade Pad', PADS, 'saw ensemble, slow brass-like swell, wheel adds brilliance', [
        L(sawLevel=1, pulseLevel=.4, filterType=OTA, cutoffHz=900, resonanceQ=1.5, filterEnvAmt=.35, filtAttackSec=1.2,
          filtDecaySec=2.5, filtSustainLvl=.6, attackSec=.9, decaySec=2, sustainLevel=.9, releaseSec=2.2, unisonVoices=2,
          unisonDetune=10, unisonSpread=.7, analog=.35, volume=.55, fx=WIDE_CHORUS)],
      mods=[BRIGHT_WHEEL(1, .3), mod(AT, 'L1_cut', .2)], reverb=.4),
    P('OB-X · Warm Pad', PADS, 'detuned saws through the OTA, gentle chorus', [
        L(sawLevel=1, osc2Level=.9, osc2Detune=9, filterType=OTA, cutoffHz=1400, resonanceQ=1.0, attackSec=.6, decaySec=2,
          sustainLevel=.9, releaseSec=1.8, analog=.3, volume=.55, fx=WARM_CHORUS)],
      mods=[BRIGHT_WHEEL(1, .25)], reverb=.35),
    P('PPG · Wave Pad', PADS, 'wavetable sweep, LFO scans the table', [
        L(synthesisType=2, wavetableIdx=4, wavetableMorph=.2, filterType=OTA, cutoffHz=3200, resonanceQ=1.0, attackSec=.5,
          decaySec=2, sustainLevel=.85, releaseSec=2.0, volume=.55, fx=WIDE_CHORUS)],
      mods=[mod(LFO1, 'L1_wtMorph', .35, True), mod(WHEEL, 'L1_wtMorph', .4)], lfos=[lfo(rateHz=.15, depth=1.0)], reverb=.4),
    P('Polysix · Ensemble Pad', PADS, 'PWM pad with the ensemble effect', [
        L(waveType=PWM_ENS, sawLevel=0, pulseLevel=1, pulseWidth=.4, pwmDepth=.25, pwmRateHz=.5, filterType=OTA, cutoffHz=1800,
          resonanceQ=.9, attackSec=.4, sustainLevel=.95, releaseSec=1.5, volume=.6, fx={CHORUS_I: {'mix': .5, 'rateHz': .4, 'depth': .9}})],
      mods=[BRIGHT_WHEEL(1, .25)], reverb=.35),
    P('Vangelis · Spiral Pad', PADS, 'CS-80 style pad with slow filter movement and long reverb', [
        L(sawLevel=.9, pulseLevel=.5, filterType=OTA, cutoffHz=700, resonanceQ=2.5, attackSec=1.4, decaySec=3, sustainLevel=.9,
          releaseSec=3.5, analog=.35, volume=.6, fx=WIDE_CHORUS)],
      mods=[mod(LFO1, 'L1_cut', .18, True), mod(AT, 'L1_cut', .25)], lfos=[lfo(rateHz=.12, depth=1.0)], reverb=.55),
    P('FM · Glass Pad', PADS, 'additive FM shimmer under a soft swell', [
        vp.fm4(7, [1.0, 2.0, 3.01, 4.02], [1.0, .45, .3, .2], [.8, 1.0, 1.2, 1.4], [3.0, 3.0, 3.0, 3.0], [.9, .7, .6, .5],
               releaseSec=2.5, volume=.55, fx=WIDE_CHORUS)],
      mods=[mod(WHEEL, 'L1_vol', .15)], reverb=.5),
    P('Analog · PWM Strings Pad', PADS, 'slow PWM, wide and lush', [
        L(waveType=PULSE, sawLevel=0, pulseLevel=1, pulseWidth=.45, pwmDepth=.3, pwmRateHz=.35, filterType=LADDER, cutoffHz=1500,
          attackSec=.7, sustainLevel=1, releaseSec=2, volume=.6, fx=WARM_CHORUS)],
      mods=[BRIGHT_WHEEL(1, .2)], reverb=.4),
    P('LA32 · Dark Drone Pad', PADS, 'LA square below the TVF middle point, slow motion', [
        L(synthesisType=7, sawLevel=0, pulseLevel=1, pulseWidth=.5, cutoffHz=160, filterKeyTrack=1.0, attackSec=1.6, decaySec=3,
          sustainLevel=1, releaseSec=3.0, osc2Level=.7, osc2Detune=12, osc2Wave=1, volume=.85, fx=WIDE_CHORUS)],
      mods=[mod(LFO1, 'L1_cut', .12, True), mod(M1, 'L1_cut', .4)], lfos=[lfo(rateHz=.08, depth=1.0)], reverb=.5),
]

# ------------------------------------------------------------------ BRASS
BRASS = 'BRASS'
BANK += [
    P('OB-X · Jump Brass', BRASS, 'the famous stab: saws, fast filter envelope', [
        L(sawLevel=1, osc2Level=.9, osc2Detune=8, filterType=OTA, cutoffHz=900, resonanceQ=1.2, filterEnvAmt=.55,
          filtAttackSec=.01, filtDecaySec=.35, filtSustainLvl=.45, attackSec=.005, sustainLevel=.9, releaseSec=.2, volume=.6,
          analog=.25, fx=WARM_CHORUS)],
      mods=[VEL_CUT(1, .25)], reverb=.2),
    P('CS-80 · Velocity Brass', BRASS, 'velocity opens the filter per voice, aftertouch swells', [
        L(sawLevel=1, pulseLevel=.3, filterType=OTA, cutoffHz=700, resonanceQ=1.5, filterEnvAmt=.45, filtAttackSec=.08,
          filtDecaySec=.6, filtSustainLvl=.5, attackSec=.04, sustainLevel=.95, releaseSec=.35, volume=.6, analog=.3,
          fx=WIDE_CHORUS)],
      mods=[VEL_CUT(1, .35), mod(AT, 'L1_cut', .2)], reverb=.3),
    P('Prophet-5 · Sync Brass', BRASS, 'synced saws, punchy filter, poly', [
        L(waveType=SYNC, sawLevel=1, osc2Level=.5, filterType=OTA, cutoffHz=1200, resonanceQ=1.4, filterEnvAmt=.5,
          filtDecaySec=.4, filtSustainLvl=.45, attackSec=.01, sustainLevel=.9, releaseSec=.25, volume=.6)],
      mods=[VEL_CUT(1, .2), BRIGHT_WHEEL(1, .2)], reverb=.2),
    P('Matrix · Brass Swell', BRASS, 'slow filter swell, wheel controls the brightness', [
        L(sawLevel=1, osc2Level=.8, osc2Detune=6, filterType=LADDER, cutoffHz=500, resonanceQ=1.3, filterEnvAmt=.5,
          filtAttackSec=.45, filtDecaySec=1.2, filtSustainLvl=.6, attackSec=.25, sustainLevel=1, releaseSec=.5, volume=.6,
          fx=WARM_CHORUS)],
      mods=[BRIGHT_WHEEL(1, .35)], reverb=.3),
    P('Juno · Synth Brass', BRASS, 'DCO brass with the chorus', [
        L(waveType=JUNO, sawLevel=.9, pulseLevel=.3, filterType=OTA, cutoffHz=1000, resonanceQ=1.1, filterEnvAmt=.45,
          filtDecaySec=.45, filtSustainLvl=.5, attackSec=.02, sustainLevel=.9, releaseSec=.3, volume=.6, fx=WARM_CHORUS)],
      mods=[VEL_CUT(1, .2)], reverb=.2),
    P('Polymoog · Brass Section', BRASS, 'two layers: low saws and bright octave', [
        L(sawLevel=1, filterType=LADDER, cutoffHz=800, filterEnvAmt=.4, filtDecaySec=.5, filtSustainLvl=.5, attackSec=.03,
          sustainLevel=.9, releaseSec=.3, volume=.5),
        L(sawLevel=.8, octave=1, filterType=OTA, cutoffHz=1600, filterEnvAmt=.3, filtDecaySec=.4, filtSustainLvl=.4,
          attackSec=.05, sustainLevel=.8, releaseSec=.3, volume=.3, pan=.2)],
      mods=[VEL_CUT(1, .25), VEL_CUT(2, .25)], reverb=.25),
    P('Stab · Disco Brass', BRASS, 'short, bright brass stab for funk', [
        L(sawLevel=1, osc2Level=.8, osc2Detune=10, filterType=OTA, cutoffHz=1500, resonanceQ=1.0, filterEnvAmt=.45,
          filtDecaySec=.2, filtSustainLvl=.2, attackSec=.003, decaySec=.35, sustainLevel=.4, releaseSec=.15, volume=.6)],
      mods=[VEL_CUT(1, .2)], reverb=.18),
    P('Brass · Soft Horns', BRASS, 'mellow horns with a slow bloom', [
        L(sawLevel=.8, pulseLevel=.4, filterType=LADDER, cutoffHz=600, resonanceQ=.9, filterEnvAmt=.3, filtAttackSec=.2,
          filtDecaySec=.8, filtSustainLvl=.6, attackSec=.12, sustainLevel=.95, releaseSec=.45, volume=.6, fx=WARM_CHORUS)],
      mods=[mod(AT, 'L1_cut', .25), AT_VIB(1)], reverb=.3),
]

# ------------------------------------------------------------------ STRINGS
STRINGS = 'STRINGS'
BANK += [
    P('Solina · String Ensemble', STRINGS, 'divide-down strings into the ensemble (Juno I+II mode)', [
        L(waveType=PWM_ENS, sawLevel=.5, pulseLevel=.8, pulseWidth=.45, pwmDepth=.15, filterType=OTA, cutoffHz=2400,
          attackSec=.35, sustainLevel=1, releaseSec=1.2, volume=.6,
          fx={CHORUS_I: {'mix': .55, 'rateHz': .6, 'depth': .9}, CHORUS_II: {'mix': .55, 'rateHz': .9, 'depth': .9}})],
      mods=[BRIGHT_WHEEL(1, .2)], reverb=.35),
    P('Mellotron · Strings', STRINGS, 'tape strings: soft attack, slight wow and flutter', [
        L(sawLevel=.7, pulseLevel=.4, filterType=LP12, cutoffHz=2200, resonanceQ=.7, attackSec=.18, sustainLevel=.95,
          releaseSec=.6, volume=.6, analog=.5, transientType=6, transientLevel=.2, transientDecay=.15)],
      mods=[mod(LFO1, 'L1_det', .08, True)], lfos=[lfo(rateHz=5.5, depth=.4, destPitch=4)], reverb=.3),
    P('Mellotron · Flute', STRINGS, 'tape flute with breath and flutter', [
        L(waveType=TRI, sawLevel=0, osc2Level=.3, osc2Wave=2, osc2Semitones=12, filterType=LP12, cutoffHz=3000,
          attackSec=.08, sustainLevel=1, releaseSec=.4, volume=.7, analog=.5, transientType=0, transientLevel=.35,
          transientDecay=.12, noiseLevel=.03)],
      lfos=[lfo(rateHz=6.0, depth=.4, destPitch=5)], reverb=.3),
    P('ARP · String Ensemble', STRINGS, 'bright ensemble strings with phaser', [
        L(sawLevel=1, filterType=OTA, cutoffHz=3200, attackSec=.25, sustainLevel=1, releaseSec=1.0, volume=.55,
          fx={CHORUS_II: {'mix': .5, 'rateHz': .8, 'depth': .85}, PHASER_I: {'mix': .3, 'rateHz': .3, 'depth': .5}})],
      mods=[BRIGHT_WHEEL(1, .2)], reverb=.35),
    P('Oberheim · Strings', STRINGS, 'warm detuned strings, slow attack', [
        L(sawLevel=1, osc2Level=.9, osc2Detune=12, filterType=OTA, cutoffHz=1800, resonanceQ=.8, attackSec=.5,
          sustainLevel=1, releaseSec=1.4, volume=.55, analog=.3, fx=WARM_CHORUS)],
      mods=[BRIGHT_WHEEL(1, .25), mod(AT, 'L1_vol', .1)], reverb=.35),
    P('Polymoog · Vox Humana', STRINGS, 'the vox humana chorus-string sound', [
        L(waveType=VOX, sawLevel=.6, pulseLevel=.4, filterType=OTA, cutoffHz=2000, attackSec=.3, sustainLevel=1,
          releaseSec=1.0, volume=.6, fx={CHORUS_I: {'mix': .6, 'rateHz': .7, 'depth': .9}})],
      reverb=.35),
    P('Chamber · Bowed Strings', STRINGS, 'expressive bowed strings: bow attack, aftertouch vibrato', [
        L(sawLevel=1, filterType=OTA, cutoffHz=1500, resonanceQ=1.1, filterEnvAmt=.2, filtAttackSec=.3, filtDecaySec=1,
          filtSustainLvl=.7, attackSec=.22, sustainLevel=1, releaseSec=.6, volume=.6, transientType=6, transientLevel=.25,
          transientDecay=.2, fx=WARM_CHORUS)],
      mods=[AT_VIB(1), mod(AT, 'L1_cut', .2), VEL_CUT(1, .15)], reverb=.35),
    P('Synth · Pizzicato', STRINGS, 'plucked strings: short decay, velocity brightness', [
        L(sawLevel=1, filterType=OTA, cutoffHz=1200, filterEnvAmt=.45, filtDecaySec=.2, filtSustainLvl=0, attackSec=.001,
          decaySec=.45, sustainLevel=0, releaseSec=.3, volume=.7, transientType=2, transientLevel=.3, transientDecay=.05)],
      mods=[VEL_CUT(1, .3)], reverb=.3),
]

# ------------------------------------------------------------------ KEYS
KEYS = 'KEYS'
BANK += [
    P('Synclavier · Tine Piano', KEYS, 'FM tine electric piano, velocity = bark', [
        vp.fm4(3, [1.0, 1.0, 1.0, 14.0], [1.0, .5, .7, .15], [.001] * 4, [3.0, 1.5, 2.5, .3], [.0, .0, .0, .0],
               velToAmp=1.0, releaseSec=.4, volume=.65, fx={CHORUS_I: {'mix': .3, 'rateHz': .513, 'depth': .7}})],
      mods=[mod(VEL, 'L1_cut', .15)], reverb=.25),
    P('Polymoog · Keyboard', KEYS, 'velocity-sensitive pulse keys', [
        L(waveType=PULSE, sawLevel=0, pulseLevel=1, pulseWidth=.35, filterType=OTA, cutoffHz=1600, filterEnvAmt=.35,
          filtDecaySec=.6, filtSustainLvl=.3, attackSec=.002, decaySec=1.2, sustainLevel=.5, releaseSec=.4, velToAmp=1.2,
          volume=.65)],
      mods=[VEL_CUT(1, .3)], reverb=.25),
    P('Clavi · Funk Synth', KEYS, 'narrow pulse, quick filter: a clavinet-like synth', [
        L(waveType=PULSE, sawLevel=0, pulseLevel=1, pulseWidth=.15, filterType=OTA, cutoffHz=2600, resonanceQ=2.0,
          filterEnvAmt=.4, filtDecaySec=.18, filtSustainLvl=.2, attackSec=.001, decaySec=.6, sustainLevel=.2, releaseSec=.08,
          volume=.7, transientType=4, transientLevel=.25)],
      mods=[VEL_CUT(1, .3), mod(WHEEL, 'L1_fx2_mix', .5)], reverb=.1),
    P('Harpsi · Synth', KEYS, 'bright plucked keys with an octave layer', [
        L(sawLevel=1, filterType=OTA, cutoffHz=3800, filterEnvAmt=.3, filtDecaySec=.4, filtSustainLvl=.1, attackSec=.001,
          decaySec=1.4, sustainLevel=0, releaseSec=.3, volume=.5, transientType=2, transientLevel=.3),
        L(waveType=PULSE, sawLevel=0, pulseLevel=.8, pulseWidth=.2, octave=1, filterType=OTA, cutoffHz=4500, attackSec=.001,
          decaySec=1.0, sustainLevel=0, releaseSec=.25, volume=.25)],
      reverb=.2),
    P('FM · Wurli Keys', KEYS, 'reedy FM piano with tremolo on the wheel', [
        vp.fm4(3, [1.0, 1.0, 1.0, 3.0], [1.0, .6, .8, .25], [.001] * 4, [2.0, 1.2, 1.8, .5], [.1, 0, 0, 0],
               velToAmp=1.0, releaseSec=.3, volume=.65)],
      lfos=[lfo(rateHz=5.5, depth=.5, destVolume=.35)],
      reverb=.2),
    P('Organ · Synth Drawbars', KEYS, 'harmonic organ waveform with a rotary-like chorus', [
        L(waveType=ORGAN, sawLevel=1, filterType=LP12, cutoffHz=6000, attackSec=.005, sustainLevel=1, releaseSec=.08,
          volume=.6, fx={CHORUS_I: {'mix': .45, 'rateHz': 6.0, 'depth': .4}})],
      mods=[mod(WHEEL, 'L1_fx0_rate', .3)], reverb=.2),
    P('Celeste · Synth', KEYS, 'soft bell-like keys', [
        L(waveType=SINE, sawLevel=0, osc2Level=.4, osc2Wave=2, osc2Semitones=12, filterType=LP12, cutoffHz=5000,
          attackSec=.002, decaySec=1.8, sustainLevel=0, releaseSec=.8, volume=.7, transientType=5, transientLevel=.3)],
      reverb=.35),
    P('Electric · Grand Synth', KEYS, 'CP-80 style: bright hammered saw + sine body', [
        L(sawLevel=.6, filterType=OTA, cutoffHz=2400, filterEnvAmt=.35, filtDecaySec=1.0, filtSustainLvl=.2, attackSec=.001,
          decaySec=2.5, sustainLevel=0, releaseSec=.5, volume=.45, transientType=1, transientLevel=.35),
        L(waveType=SINE, sawLevel=0, attackSec=.001, decaySec=3.0, sustainLevel=0, releaseSec=.6, volume=.4)],
      mods=[VEL_CUT(1, .3)], reverb=.25),
]

# ------------------------------------------------------------------ BELLS
BELLS = 'BELLS'
BANK += [
    P('Synclavier · Bells', BELLS, 'inharmonic FM bells with a long ring', [
        vp.fm4(4, [1.0, 1.0, 1.0, 3.5], [1.0, .8, .6, .35], [.001] * 4, [4.0, 3.0, 2.5, 1.5], [0, 0, 0, 0],
               releaseSec=2.0, volume=.6)],
      mods=[mod(VEL, 'L1_cut', .2)], reverb=.45),
    P('FM · Tubular Bells', BELLS, 'church-like tubular bell', [
        vp.fm4(0, [1.0, 3.5, 1.0, 1.0], [1.0, .55, .2, .1], [.001] * 4, [6.0, 3.0, 1.0, 1.0], [0, 0, 0, 0],
               releaseSec=3.0, volume=.6)],
      reverb=.5),
    P('Vibraphone · Synth', BELLS, 'soft mallet with motor tremolo on the wheel', [
        L(waveType=SINE, sawLevel=0, osc2Level=.25, osc2Semitones=24, osc2Wave=2, attackSec=.001, decaySec=2.5, sustainLevel=0,
          releaseSec=1.2, volume=.75, transientType=1, transientLevel=.4, transientDecay=.05)],
      lfos=[lfo(rateHz=5.0, depth=.6, destVolume=.3)], reverb=.35),
    P('Marimba · FM', BELLS, 'woody FM marimba', [
        vp.fm4(0, [1.0, 4.0, 1.0, 1.0], [1.0, .5, .1, .1], [.001] * 4, [.6, .15, .1, .1], [0, 0, 0, 0],
               releaseSec=.4, volume=.7)],
      reverb=.25),
    P('Kalimba · Thumb Piano', BELLS, 'bright pluck with a metallic tine', [
        vp.fm4(3, [1.0, 1.0, 1.0, 7.0], [1.0, .35, .5, .2], [.001] * 4, [1.2, .5, .8, .15], [0, 0, 0, 0],
               releaseSec=.5, volume=.7)],
      reverb=.3),
    P('Glass · Harmonica', BELLS, 'sine glass with a soft rub', [
        L(waveType=SINE, sawLevel=0, osc2Level=.3, osc2Semitones=12, osc2Wave=2, attackSec=.15, sustainLevel=1, releaseSec=1.2,
          volume=.75, transientType=5, transientLevel=.15)],
      lfos=[lfo(rateHz=4.5, depth=.3, destPitch=3)], reverb=.5),
    P('Music Box', BELLS, 'tiny metallic comb, high register', [
        vp.fm4(3, [1.0, 1.0, 1.0, 5.0], [1.0, .4, .5, .3], [.001] * 4, [1.5, .6, 1.0, .2], [0, 0, 0, 0],
               octave=1, releaseSec=.8, volume=.6)],
      reverb=.45),
    P('Gamelan · Ring Bells', BELLS, 'ring-modulated LA32 partials', [
        L(synthesisType=7, sawLevel=0, pulseLevel=1, cutoffHz=2800, attackSec=.001, decaySec=3.0, sustainLevel=0,
          releaseSec=2.0, osc2Level=1.0, osc2Semitones=17, osc2Detune=20, laRing=1.0, volume=.6)],
      reverb=.45, delay=(500, .3, .15)),
]

# ------------------------------------------------------------------ PLUCKS
PLUCKS = 'PLUCKS'
BANK += [
    P('Juno · Pluck', PLUCKS, 'short DCO pluck with chorus', [
        L(waveType=JUNO, sawLevel=1, filterType=OTA, cutoffHz=900, filterEnvAmt=.5, filtDecaySec=.25, filtSustainLvl=0,
          attackSec=.001, decaySec=.5, sustainLevel=0, releaseSec=.3, volume=.7, fx=WARM_CHORUS)],
      mods=[VEL_CUT(1, .25)], reverb=.3, delay=(375, .3, .15)),
    P('Trance · Supersaw Pluck', PLUCKS, 'bright supersaw stab', [
        L(waveType=SUPERSAW, sawLevel=1, filterType=OTA, cutoffHz=1200, filterEnvAmt=.55, filtDecaySec=.22, filtSustainLvl=0,
          attackSec=.001, decaySec=.35, sustainLevel=0, releaseSec=.25, volume=.6)],
      mods=[mod(M1, 'L1_fDec', .4)], reverb=.35, delay=(375, .35, .2)),
    P('Koto · FM Pluck', PLUCKS, 'plucked FM string', [
        vp.fm4(2, [1.0, 3.0, 1.0, 1.0], [1.0, .45, .4, .1], [.001] * 4, [1.4, .3, .5, .2], [0, 0, 0, 0],
               releaseSec=.4, volume=.7)],
      reverb=.3),
    P('Harp · Synth', PLUCKS, 'soft harp-like pluck', [
        L(waveType=TRI, sawLevel=0, osc2Level=.4, osc2Wave=0, filterType=OTA, cutoffHz=2000, filterEnvAmt=.3, filtDecaySec=.5,
          attackSec=.001, decaySec=1.4, sustainLevel=0, releaseSec=.9, volume=.75, transientType=2, transientLevel=.3)],
      reverb=.4),
    P('Pro-One · Pluck', PLUCKS, 'snappy mono-synth pluck', [
        L(sawLevel=1, pulseLevel=.5, filterType=LADDER, cutoffHz=600, resonanceQ=3.5, filterEnvAmt=.55, filtDecaySec=.15,
          filtSustainLvl=0, attackSec=.001, decaySec=.3, sustainLevel=0, releaseSec=.15, volume=.7)],
      mods=[VEL_CUT(1, .3)], reverb=.2),
    P('Mallet · Pluck Pad', PLUCKS, 'pluck attack with a soft sustaining tail', [
        L(sawLevel=1, filterType=OTA, cutoffHz=1000, filterEnvAmt=.4, filtDecaySec=.3, filtSustainLvl=.2, attackSec=.001,
          decaySec=1.0, sustainLevel=.35, releaseSec=1.2, volume=.6, transientType=1, transientLevel=.3, fx=WIDE_CHORUS)],
      reverb=.4),
    P('Nylon · Synth Guitar', PLUCKS, 'nylon-like pluck with a body resonance', [
        L(sawLevel=.6, pulseLevel=.5, pulseWidth=.3, filterType=OTA, cutoffHz=2200, resonanceQ=1.2, filterEnvAmt=.35,
          filtDecaySec=.35, attackSec=.001, decaySec=1.2, sustainLevel=0, releaseSec=.4, volume=.75, transientType=2,
          transientLevel=.4)],
      mods=[VEL_CUT(1, .25)], reverb=.3),
    P('Sync · Pluck', PLUCKS, 'bright hard-sync pluck', [
        L(waveType=SYNC_SWEEP, sawLevel=1, osc2Level=.5, filterType=OTA, cutoffHz=3000, filterEnvAmt=.3, filtDecaySec=.25,
          attackSec=.001, decaySec=.4, sustainLevel=0, releaseSec=.2, volume=.6)],
      reverb=.25, delay=(300, .3, .15)),
]

# ------------------------------------------------------------------ SEQUENCES (sequencer on, synced to the master tempo)
SEQ = 'SEQUENCES'
BANK += [
    P('Berlin School · Sequence', SEQ, 'Tangerine Dream-style ladder sequence, macro = filter', [
        L(sawLevel=1, filterType=LADDER, cutoffHz=500, resonanceQ=5.0, filterEnvAmt=.45, filtDecaySec=.15, filtSustainLvl=.1,
          attackSec=.001, sustainLevel=.6, releaseSec=.1, volume=.7, octave=-1,
          **seq([0, 12, 7, 12, 3, 12, 7, 15, 0, 12, 7, 12, 5, 12, 10, 7], accent=(0, 8), gate=.5))],
      mods=[mod(M1, 'L1_cut', .45), mod(LFO1, 'L1_cut', .1, True)], lfos=[lfo(rateHz=.1, depth=1.0)], macros=[.2, 0, 0, 0],
      clock=True, delay=(375, .35, .2), reverb=.25),
    P('Juno · Arp Chords', SEQ, 'arpeggio-like up pattern with chorus', [
        L(waveType=JUNO, sawLevel=1, filterType=OTA, cutoffHz=1500, filterEnvAmt=.3, filtDecaySec=.2, attackSec=.001,
          decaySec=.3, sustainLevel=.3, releaseSec=.15, volume=.65, fx=WARM_CHORUS,
          **seq([0, 4, 7, 12, 16, 12, 7, 4], steps=8, gate=.6))],
      clock=True, reverb=.3),
    P('Acid · Two-Layer Groove', SEQ, '303 line + octave layer, accents and slides', [
        L(sawLevel=1, filterType=LADDER, cutoffHz=380, resonanceQ=8.0, filterEnvAmt=.6, filtDecaySec=.18, filtSustainLvl=0,
          attackSec=.001, sustainLevel=.7, releaseSec=.05, octave=-1, volume=.7, drive=.3,
          **seq([0, 0, 12, 0, 7, 0, 10, 0, 0, 3, 0, 12, 0, 5, 7, 0], accent=(2, 6, 11), slide=(4, 12), gate=.45)),
        L(waveType=PULSE, sawLevel=0, pulseLevel=.8, filterType=OTA, cutoffHz=900, filterEnvAmt=.4, filtDecaySec=.12,
          attackSec=.001, sustainLevel=.4, releaseSec=.05, volume=.35, pan=.25,
          **seq([12, None, 24, None, 19, None, 22, None, 12, None, 15, None, 24, None, 19, None], gate=.3))],
      mods=[mod(M1, 'L1_cut', .45)], clock=True, delay=(375, .25, .12)),
    P('Pro-One · Seq Bass', SEQ, 'driving 16th bass pattern', [
        L(sawLevel=1, pulseLevel=.4, filterType=LADDER, cutoffHz=450, resonanceQ=3.0, filterEnvAmt=.5, filtDecaySec=.12,
          attackSec=.001, sustainLevel=.6, releaseSec=.05, octave=-1, volume=.75,
          **seq([0, 0, 12, 0, 0, 0, 10, 12, 0, 0, 7, 0, 0, 0, 5, 7], accent=(0, 6), gate=.4))],
      mods=[mod(M1, 'L1_cut', .4)], clock=True),
    P('Eighties · Bass Pulse', SEQ, 'octave-jumping 8th bass with a slight delay', [
        L(waveType=PULSE, sawLevel=0, pulseLevel=1, pulseWidth=.35, filterType=OTA, cutoffHz=700, filterEnvAmt=.3,
          filtDecaySec=.15, attackSec=.001, sustainLevel=.7, releaseSec=.05, octave=-1, volume=.75,
          **seq([0, 12, 0, 12, 0, 12, 0, 12], steps=8, division=1, gate=.5))],
      clock=True, delay=(375, .2, .12)),
    P('Pluck · Polyrhythm', SEQ, 'two layers with different lengths (5 and 8 steps)', [
        L(waveType=SINE, sawLevel=0, osc2Level=.3, osc2Semitones=12, attackSec=.001, decaySec=.3, sustainLevel=0,
          releaseSec=.2, volume=.6, transientType=1, transientLevel=.3, **seq([0, 7, 12, 3, 10], steps=5, gate=.5)),
        L(waveType=TRI, sawLevel=0, attackSec=.001, decaySec=.25, sustainLevel=0, releaseSec=.2, octave=1, volume=.35, pan=.3,
          **seq([0, None, 5, None, 7, None, 12, None], steps=8, gate=.4))],
      clock=True, reverb=.35, delay=(300, .3, .15)),
    P('Trance · Gate Pad', SEQ, 'pad chopped by the sequencer gate', [
        L(waveType=SUPERSAW, sawLevel=1, filterType=OTA, cutoffHz=2200, attackSec=.01, sustainLevel=1, releaseSec=.1,
          volume=.55, **seq([0] * 16, gate=.4, accent=(0, 4, 8, 12)))],
      mods=[mod(M1, 'L1_seqGate', .5)], clock=True, reverb=.35),
    P('Sci-Fi · Random Sequence', SEQ, 'random filter on each note, sample-and-hold feel', [
        L(sawLevel=.6, pulseLevel=.6, filterType=LADDER, cutoffHz=700, resonanceQ=6.0, attackSec=.001, sustainLevel=.6,
          releaseSec=.08, volume=.6, **seq([0, 3, 7, 10, 12, 10, 7, 3], steps=8, gate=.45))],
      mods=[mod(RND, 'L1_cut', .3, True)], clock=True, delay=(375, .4, .2), reverb=.25),
]

# ------------------------------------------------------------------ CHOIR / VOX
CHOIR = 'CHOIR'
BANK += [
    P('Vox · Synth Choir', CHOIR, 'vocal waveform with a breath attack', [
        L(waveType=VOX, sawLevel=1, filterType=OTA, cutoffHz=2200, attackSec=.3, sustainLevel=1, releaseSec=1.2, volume=.6,
          transientType=0, transientLevel=.25, fx=WIDE_CHORUS)],
      reverb=.45),
    P('Mellotron · Choir', CHOIR, 'tape choir: soft "aah" with flutter', [
        L(synthesisType=7, sawLevel=0, pulseLevel=1, pulseWidth=.2, cutoffHz=900, resonanceQ=3.0, attackSec=.25,
          sustainLevel=.95, releaseSec=.8, osc2Level=.6, osc2Detune=8, osc2Wave=1, volume=.65, transientType=0,
          transientLevel=.3, analog=.4, fx={CHORUS_I: {'mix': .5, 'rateHz': .5, 'depth': .8}})],
      lfos=[lfo(rateHz=5.5, depth=.4, destPitch=4)], reverb=.45),
    P('Vocal · Formant Pad', CHOIR, 'vocal spectral wavetable, wheel opens the vowel', [
        L(waveType=26, sawLevel=1, filterType=BP12, cutoffHz=900, resonanceQ=2.5, attackSec=.4, sustainLevel=1, releaseSec=1.5,
          volume=.65, fx=WIDE_CHORUS)],
      mods=[mod(WHEEL, 'L1_cut', .3), mod(LFO1, 'L1_cut', .08, True)], lfos=[lfo(rateHz=.25, depth=1.0)], reverb=.45),
    P('Choir · Ooh', CHOIR, 'dark round voices', [
        L(waveType=VOX, sawLevel=1, filterType=LP12, cutoffHz=1100, attackSec=.5, sustainLevel=1, releaseSec=1.6,
          volume=.7, fx=WARM_CHORUS)],
      reverb=.5),
    P('Choir · Bright Aah', CHOIR, 'open choir with a high octave layer', [
        L(waveType=VOX, sawLevel=1, filterType=OTA, cutoffHz=2800, attackSec=.35, sustainLevel=1, releaseSec=1.2, volume=.5),
        L(waveType=VOX, sawLevel=.8, octave=1, filterType=OTA, cutoffHz=3200, attackSec=.5, sustainLevel=1, releaseSec=1.2,
          volume=.25, pan=.25)],
      reverb=.5),
    P('Whisper · Voices', CHOIR, 'breathy noise-band voices', [
        L(waveType=34, sawLevel=1, filterType=BP12, cutoffHz=1200, resonanceQ=4.0, attackSec=.6, sustainLevel=1,
          releaseSec=1.5, volume=.75)],
      mods=[mod(KEY, 'L1_cut', .25, True)], reverb=.55),
    P('LA32 · Space Voices', CHOIR, 'LA narrow pulses with chiff, D-50 style', [
        L(synthesisType=7, sawLevel=0, pulseLevel=1, pulseWidth=.15, cutoffHz=1500, resonanceQ=4.5, attackSec=.4,
          sustainLevel=.9, releaseSec=1.8, osc2Level=.7, osc2Detune=10, osc2Wave=1, volume=.6, transientType=3,
          transientLevel=.3, fx=WIDE_CHORUS)],
      reverb=.5, delay=(450, .35, .15)),
    P('Vox · Humana Organ', CHOIR, 'vox humana with tremolo', [
        L(waveType=VOX, sawLevel=.8, pulseLevel=.4, filterType=OTA, cutoffHz=1800, attackSec=.15, sustainLevel=1,
          releaseSec=.6, volume=.65)],
      lfos=[lfo(rateHz=5.8, depth=.5, destVolume=.3)], reverb=.35),
]

# ------------------------------------------------------------------ ORGAN
ORGAN_CAT = 'ORGAN'
BANK += [
    P('Organ · Full Drawbars', ORGAN_CAT, 'harmonic organ, rotary chorus on the wheel', [
        L(waveType=ORGAN, sawLevel=1, filterType=LP12, cutoffHz=7000, attackSec=.004, sustainLevel=1, releaseSec=.06,
          volume=.6, fx={CHORUS_I: {'mix': .4, 'rateHz': 1.2, 'depth': .5}})],
      mods=[mod(WHEEL, 'L1_fx0_rate', .55)], reverb=.2),
    P('Organ · Jazz', ORGAN_CAT, 'octave-fifth organ with a key click', [
        L(waveType=OCT_FIFTH, sawLevel=1, filterType=LP12, cutoffHz=5000, attackSec=.003, sustainLevel=1, releaseSec=.05,
          volume=.65, transientType=4, transientLevel=.2, transientDecay=.01)],
      reverb=.2),
    P('Combo · Transistor Organ', ORGAN_CAT, 'bright buzzy combo organ', [
        L(waveType=PULSE, sawLevel=.4, pulseLevel=.8, pulseWidth=.4, filterType=OTA, cutoffHz=4000, attackSec=.003,
          sustainLevel=1, releaseSec=.05, volume=.55)],
      lfos=[lfo(rateHz=6.5, depth=.4, destPitch=8)], reverb=.2),
    P('Organ · Church Pipes', ORGAN_CAT, 'soft pipes with slow speech', [
        L(waveType=HARMONIC, sawLevel=1, filterType=LP12, cutoffHz=3500, attackSec=.12, sustainLevel=1, releaseSec=.4,
          volume=.65)],
      reverb=.6),
    P('Organ · Gospel Perc', ORGAN_CAT, 'organ with a percussive harmonic', [
        L(waveType=ORGAN, sawLevel=1, filterType=LP12, cutoffHz=6000, attackSec=.003, sustainLevel=1, releaseSec=.05,
          volume=.5),
        L(waveType=SINE, sawLevel=0, octave=1, semitones=7, attackSec=.001, decaySec=.35, sustainLevel=0, releaseSec=.1,
          volume=.35)],
      mods=[mod(WHEEL, 'L1_fx0_rate', .5)], reverb=.25),
    P('Organ · Reed', ORGAN_CAT, 'reedy harmonium', [
        L(waveType=PULSE, sawLevel=.3, pulseLevel=.8, pulseWidth=.3, filterType=LP12, cutoffHz=2500, attackSec=.06,
          sustainLevel=1, releaseSec=.2, volume=.6, fx=WARM_CHORUS)],
      reverb=.3),
    P('Organ · Prog Lead', ORGAN_CAT, 'driven organ for leads', [
        L(waveType=ORGAN, sawLevel=1, filterType=LP12, cutoffHz=4500, drive=.5, attackSec=.003, sustainLevel=1,
          releaseSec=.05, volume=.5)],
      mods=[mod(M1, 'L1_drive', .4)], reverb=.2),
    P('Organ · Theatre', ORGAN_CAT, 'tremulant theatre organ', [
        L(waveType=TRI_ENS, sawLevel=1, filterType=LP12, cutoffHz=4000, attackSec=.02, sustainLevel=1, releaseSec=.2,
          volume=.65)],
      lfos=[lfo(rateHz=6.2, depth=.5, destPitch=10, destVolume=.2)], reverb=.45),
]

# ------------------------------------------------------------------ FX
FX = 'FX'
BANK += [
    P('FX · Rising Sweep', FX, 'noise and saw sweeping up over the filter envelope', [
        L(sawLevel=.6, noiseLevel=.5, filterType=OTA, cutoffHz=200, resonanceQ=4.0, filterEnvAmt=.9, filtAttackSec=3.5,
          filtDecaySec=1, filtSustainLvl=1, attackSec=1.0, sustainLevel=1, releaseSec=1.5, volume=.55)],
      reverb=.5),
    P('FX · Wind', FX, 'filtered noise wind, wheel = gust', [
        L(waveType=5, sawLevel=1, filterType=BP12, cutoffHz=800, resonanceQ=3.0, attackSec=1.5, sustainLevel=1,
          releaseSec=2.0, volume=.7)],
      mods=[mod(LFO1, 'L1_cut', .25, True), BRIGHT_WHEEL(1, .4)], lfos=[lfo(rateHz=.2, depth=1.0)], reverb=.5),
    P('FX · Laser', FX, 'pitch-sweep zap', [
        L(sawLevel=1, filterType=OTA, cutoffHz=3000, attackSec=.001, decaySec=.25, sustainLevel=0, releaseSec=.1,
          volume=.55, slide=24)],
      mods=[mod(AENV, 'L1_semi', .4)], delay=(250, .4, .2)),
    P('FX · Ring Mod Robot', FX, 'ring-modulated metallic voice', [
        L(waveType=12, sawLevel=1, filterType=OTA, cutoffHz=2500, attackSec=.01, sustainLevel=1, releaseSec=.2, volume=.55)],
      mods=[mod(LFO1, 'L1_det', .2, True)], lfos=[lfo(rateHz=7.0, depth=1.0)], reverb=.2),
    P('FX · Sci-Fi Drone', FX, 'evolving resonant drone', [
        L(sawLevel=.8, osc2Level=.8, osc2Semitones=7, osc2Detune=15, filterType=LADDER, cutoffHz=400, resonanceQ=7.0,
          attackSec=2.0, sustainLevel=1, releaseSec=3.0, volume=.55)],
      mods=[mod(LFO1, 'L1_cut', .25, True), mod(LFO2, 'L1_res', .15, True)],
      lfos=[lfo(rateHz=.07, depth=1.0), lfo(rateHz=.11, depth=1.0)], reverb=.6),
    P('FX · Sample & Hold', FX, 'classic random filter steps', [
        L(sawLevel=.7, pulseLevel=.5, filterType=LADDER, cutoffHz=900, resonanceQ=7.0, attackSec=.01, sustainLevel=1,
          releaseSec=.3, volume=.55)],
      mods=[mod(LFO1, 'L1_cut', .3, True)], lfos=[lfo(waveType=4, rateHz=8.0, depth=1.0)], delay=(375, .4, .2)),
    P('FX · Bit Shatter', FX, '8-bit pulse arpeggio texture', [
        L(waveType=35, sawLevel=1, filterType=LP12, cutoffHz=6000, attackSec=.001, decaySec=.2, sustainLevel=.3,
          releaseSec=.1, volume=.5)],
      delay=(187, .5, .25)),
    P('FX · Impact Hit', FX, 'low boom with a noise crack', [
        L(sawLevel=.5, subLevel=1, octave=-2, filterType=LADDER, cutoffHz=300, filterEnvAmt=.6, filtDecaySec=.4,
          attackSec=.001, decaySec=1.5, sustainLevel=0, releaseSec=1.0, volume=.85, transientType=4, transientLevel=.6)],
      reverb=.5),
]

assert len(BANK) == 96, len(BANK)
CATEGORIES = [BASS, LEADS, PADS, BRASS, STRINGS, KEYS, BELLS, PLUCKS, SEQ, CHOIR, ORGAN_CAT, FX]


def main():
    gains = {}
    if '--gains' in sys.argv:
        gains = {int(k): float(v) for k, v in json.loads(Path(sys.argv[sys.argv.index('--gains') + 1]).read_text()).items()}
    # Kept presets are read before anything is deleted (from their old or new ids).
    kept = []
    marker = FACTORY / '.bank_v323'
    for old in range(KEPT_FIRST, KEPT_LAST + 1):
        src = FACTORY / f'{(old - KEPT_FIRST + 97) if marker.exists() else old:03d}.json'
        kept.append(json.loads(src.read_text(encoding='utf8')))
    for f in FACTORY.glob('*.json'):
        f.unlink()
    catalog = []
    for offset, entry in enumerate(BANK):
        pid = 1 + offset
        params = copy.deepcopy(entry['params'])
        params['synthPresetId'] = pid
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
    kept_cats = []
    for offset, doc in enumerate(kept):
        pid = 97 + offset
        doc['factoryId'] = pid
        doc['params']['synthPresetId'] = pid
        (FACTORY / f'{pid:03d}.json').write_text(json.dumps(doc, indent=1, ensure_ascii=False), encoding='utf8')
        catalog.append({'id': pid, 'name': doc['name'], 'category': doc['category']})
        if doc['category'] not in kept_cats:
            kept_cats.append(doc['category'])
    marker.write_text('kept presets live at 97-184', encoding='utf8')
    text = CATALOG.read_text(encoding='utf8')
    m = re.search(r'export const SYNTH_FACTORY: readonly FactoryPatch\[\] = (\[.*?\]);\n', text, re.S)
    text = text.replace(m.group(1), json.dumps(catalog, ensure_ascii=False, separators=(',', ':')))
    m = re.search(r'export const SYNTH_FACTORY_CATEGORIES: readonly string\[\] = (\[.*?\]);', text)
    text = text.replace(m.group(1), json.dumps(CATEGORIES + kept_cats, ensure_ascii=False))
    text = re.sub(r'^// .*$', '// v32.3 factory bank: 96 designs (tools/build_factory_bank.py) + 48 vintage, 24 DX7, 16 LA32.', text, count=1, flags=re.M)
    CATALOG.write_text(text, encoding='utf8')
    print(f'wrote 1-96 (new) and 97-{96 + len(kept)} (kept, previously {KEPT_FIRST}-{KEPT_LAST})')


if __name__ == '__main__':
    main()
