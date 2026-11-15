"""Engine factory bank (IDs 433-472), v32.2.

  433-456  "DX7 FM"  - real DX7 voices from the factory cartridges bundled with Dexed
                       (Dexed_01 / SynprezFM, presets/synth/dx7/factory), played by msfa.
  457-472  "LA32"    - original LA designs for the LA32 synthesized-partial engine
                       (synthesis type 7), in the style of classic MT-32 / D-50 sounds.
                       No Roland ROM data is used: the PCM-style attacks come from the
                       engine's procedural attack transients.

    python tools/build_engine_presets.py

Writes presets/synth/factory/433.json ... 472.json and the catalogue entries in
webui/src/data/synthFactory.ts (after the 432 existing patches).
"""
from __future__ import annotations
import copy, json, re
from pathlib import Path

import build_vintage_presets as vp

ROOT = Path(__file__).resolve().parents[1]
FACTORY = ROOT / 'presets/synth/factory'
CARTRIDGES = ROOT / 'presets/synth/dx7/factory'
CATALOG = ROOT / 'webui/src/data/synthFactory.ts'
FIRST_ID = 433
DX7_CAT, LA_CAT = 'DX7 FM', 'LA32'
CHORUS_I, CHORUS_II, PHASER_I, PHASER_II, ECHO = range(5)


# ------------------------------------------------------------------ DX7 sysex (same as src/dsp/Dx7Engine.cpp)
def unpack_voice(p: bytes) -> bytes:
    v = bytearray(156)
    for op in range(6):
        s, d = p[op * 17:op * 17 + 17], op * 21
        for i in range(11):
            v[d + i] = s[i] & 0x7f
        v[d + 11] = s[11] & 3; v[d + 12] = (s[11] >> 2) & 3; v[d + 13] = s[12] & 7
        v[d + 14] = s[13] & 3; v[d + 15] = (s[13] >> 2) & 7; v[d + 16] = s[14] & 0x7f
        v[d + 17] = s[15] & 1; v[d + 18] = (s[15] >> 1) & 0x1f; v[d + 19] = s[16] & 0x7f; v[d + 20] = (s[12] >> 3) & 0x0f
    for i in range(8):
        v[126 + i] = p[102 + i] & 0x7f
    v[134] = p[110] & 0x1f; v[135] = p[111] & 7; v[136] = (p[111] >> 3) & 1
    for i, k in enumerate(range(112, 116)):
        v[137 + i] = p[k] & 0x7f
    v[141] = p[116] & 1; v[142] = (p[116] >> 1) & 7; v[143] = (p[116] >> 4) & 7; v[144] = p[117] & 0x7f
    for i in range(10):
        v[145 + i] = p[118 + i] & 0x7f
    v[155] = 0x3f
    return bytes(v)


def dx7_voice(cart: str, index: int) -> tuple[str, str]:
    data = (CARTRIDGES / f'{cart}.syx').read_bytes()
    assert data[0] == 0xF0 and data[3] == 0x09, cart
    voice = unpack_voice(data[6 + index * 128: 6 + index * 128 + 128])
    return voice[145:155].decode('latin1').strip(), voice.hex()


def dx7_patch(title, cart, index, inspiration, fx=None, reverb=.18, volume=.75, pan=0.0, delay=None):
    name, hexvoice = dx7_voice(cart, index)
    lay = vp.layer(synthesisType=5, filterType=0, cutoffHz=20000.0, volume=volume, pan=pan, fx=fx or {})
    lay['dx7Voice'] = hexvoice
    return vp.patch(f'DX7 · {title}', DX7_CAT, f'{inspiration} ({cart} · {name})', [lay], reverb=reverb, delay=delay)


def la(**kw):
    """LA32 layer: partial 1 = mixer (SAW / PULSE, PW), partial 2 = OSC 2, RING = laRing.
    cutoffHz + filter envelope = the TVF (cutoff follows the key with filterKeyTrack)."""
    base = dict(synthesisType=7, filterKeyTrack=1.0, filterType=0, resonanceQ=.5, velToCutoff=.25)
    base.update(kw)
    return vp.layer(**base)


BANK = []
# ------------------------------------------------------------------ DX7 FM (real DX7 voices)
EP_CHORUS = {CHORUS_I: {'mix': .35, 'rateHz': .513, 'depth': .75}}
BANK += [
    dx7_patch('Rhodes', 'SynprezFM_04', 22, 'tine electric piano', fx=EP_CHORUS),
    dx7_patch('DL Rhodes', 'SynprezFM_08', 0, 'tine electric piano', fx=EP_CHORUS),
    dx7_patch('E.Piano 11', 'SynprezFM_31', 12, 'the classic FM electric piano', fx=EP_CHORUS),
    dx7_patch('E.Piano 44', 'SynprezFM_15', 3, 'bright FM electric piano', fx=EP_CHORUS),
    dx7_patch('FM Rhodes 18', 'SynprezFM_02', 15, 'soft FM tines'),
    dx7_patch('Wurlitzer', 'SynprezFM_28', 29, 'reed electric piano', fx={PHASER_I: {'mix': .25, 'rateHz': .5, 'depth': .4}}),
    dx7_patch('Piano 3', 'SynprezFM_01', 0, 'FM acoustic piano', reverb=.22),
    dx7_patch('FM Bass', 'SynprezFM_03', 17, 'the punchy FM synth bass', reverb=0.0, volume=.85),
    dx7_patch('E.Bass K', 'SynprezFM_05', 15, 'fingered electric bass', reverb=0.0, volume=.85),
    dx7_patch('Metal Bass', 'SynprezFM_04', 30, 'metallic FM bass', reverb=0.0, volume=.8),
    dx7_patch('Brass', 'SynprezFM_04', 7, 'FM brass section', reverb=.2),
    dx7_patch('Solo Brass', 'SynprezFM_07', 0, 'FM solo brass', reverb=.2),
    dx7_patch('Strings 1', 'SynprezFM_10', 1, 'FM string ensemble', fx={CHORUS_II: {'mix': .35, 'rateHz': .863, 'depth': .75}}, reverb=.3),
    dx7_patch('Glass Bell', 'SynprezFM_01', 8, 'glassy FM bell', reverb=.32, delay=(375, .3, .15)),
    dx7_patch('Tine Chimes', 'SynprezFM_24', 14, 'chime tines', reverb=.3),
    dx7_patch('Marimba 2', 'SynprezFM_30', 22, 'FM marimba', reverb=.2),
    dx7_patch('Chorus Vibes', 'SynprezFM_28', 21, 'vibraphone', reverb=.25),
    dx7_patch('Harpsichord 1', 'SynprezFM_01', 1, 'FM harpsichord', reverb=.18),
    dx7_patch('Clavinet 50', 'SynprezFM_12', 7, 'funky clavinet', reverb=.08),
    dx7_patch('Rock Organ', 'SynprezFM_20', 25, 'FM drawbar organ', reverb=.15),
    dx7_patch('Pipe Organ', 'SynprezFM_04', 8, 'FM pipe organ', reverb=.4),
    dx7_patch('Jazz Flute', 'SynprezFM_31', 27, 'breathy FM flute', reverb=.25),
    dx7_patch('Koto', 'SynprezFM_18', 7, 'plucked koto', reverb=.25),
    dx7_patch('Wine Glass', 'SynprezFM_10', 11, 'glass harmonica', reverb=.35),
]

# ------------------------------------------------------------------ LA32 (MT-32 / D-50 style designs)
LA_CHORUS = {CHORUS_I: {'mix': .4, 'rateHz': .55, 'depth': .7}}
BANK += [
    vp.patch('LA32 · Syn Brass', LA_CAT, 'LA sawtooth brass: TVF envelope swell, slight resonance', [
        la(sawLevel=1.0, cutoffHz=700, resonanceQ=2.5, filterEnvAmt=.55, filtAttackSec=.06, filtDecaySec=.6, filtSustainLvl=.45,
           attackSec=.03, decaySec=1.0, sustainLevel=.9, releaseSec=.25, osc2Level=.6, osc2Detune=7, volume=.7, fx=LA_CHORUS)],
        reverb=.18),
    vp.patch('LA32 · Fantasy Bells', LA_CAT, 'glassy attack over a soft LA square pad (D-50 style)', [
        la(sawLevel=0.0, pulseLevel=1.0, pulseWidth=.5, cutoffHz=900, resonanceQ=4.0, filterEnvAmt=.3, filtDecaySec=1.5,
           filtSustainLvl=.4, attackSec=.2, decaySec=2.0, sustainLevel=.8, releaseSec=2.2,
           transientType=5, transientLevel=.75, transientDecay=1.1, transientTone=.75, volume=.62,
           fx={CHORUS_II: {'mix': .45, 'rateHz': .863, 'depth': .75}})],
        reverb=.38, delay=(375, .35, .18)),
    vp.patch('LA32 · Soundtrack Pad', LA_CAT, 'slow resonant LA sweep with two detuned partials', [
        la(sawLevel=1.0, cutoffHz=400, resonanceQ=6.0, filterEnvAmt=.6, filtAttackSec=2.5, filtDecaySec=3.0, filtSustainLvl=.5,
           attackSec=1.2, decaySec=2.0, sustainLevel=.9, releaseSec=3.0, osc2Level=.8, osc2Detune=11, volume=.6, fx=LA_CHORUS)],
        reverb=.4),
    vp.patch('LA32 · Staccato Heaven', LA_CAT, 'bright LA pluck with a chiff attack and echo', [
        la(sawLevel=0.0, pulseLevel=1.0, pulseWidth=.3, cutoffHz=1800, resonanceQ=3.0, filterEnvAmt=.5, filtDecaySec=.35,
           filtSustainLvl=0.0, attackSec=.002, decaySec=.6, sustainLevel=0.0, releaseSec=.5,
           transientType=3, transientLevel=.5, transientDecay=.08, transientTone=.8, volume=.7)],
        reverb=.3, delay=(300, .4, .25)),
    vp.patch('LA32 · Fretless Bass', LA_CAT, 'round LA sawtooth bass, TVF follows the key', [
        la(sawLevel=1.0, octave=-1, cutoffHz=260, resonanceQ=1.0, filterEnvAmt=.35, filtDecaySec=.5, filtSustainLvl=.3,
           attackSec=.01, decaySec=1.2, sustainLevel=.7, releaseSec=.15, volume=.9)],
        reverb=0.0, mono=(True, .03)),
    vp.patch('LA32 · Slap Bass', LA_CAT, 'LA square bass with a click attack (MT-32 style)', [
        la(sawLevel=0.0, pulseLevel=1.0, pulseWidth=.4, octave=-1, cutoffHz=600, resonanceQ=2.0, filterEnvAmt=.7,
           filtDecaySec=.18, filtSustainLvl=.15, attackSec=.001, decaySec=.6, sustainLevel=.5, releaseSec=.1,
           transientType=4, transientLevel=.6, transientDecay=.03, transientTone=.6, volume=.85)],
        reverb=0.0, mono=(True, 0)),
    vp.patch('LA32 · Square Lead', LA_CAT, 'resonant LA square lead with delayed vibrato', [
        la(sawLevel=0.0, pulseLevel=1.0, pulseWidth=.5, cutoffHz=2200, resonanceQ=7.0, filterEnvAmt=.25, filtDecaySec=.8,
           filtSustainLvl=.6, attackSec=.01, sustainLevel=1.0, releaseSec=.25, volume=.6)],
        lfos=[vp.lfo(rateHz=5.5, depth=1.0, destPitch=10, delaySec=.35, fadeSec=.4, keySync=True)],
        mono=(True, .05), delay=(330, .3, .15), reverb=.15),
    vp.patch('LA32 · Choir', LA_CAT, 'narrow-pulse LA partials with a breath attack and chorus', [
        la(sawLevel=0.0, pulseLevel=1.0, pulseWidth=.18, cutoffHz=1100, resonanceQ=3.5, attackSec=.35, decaySec=1.5,
           sustainLevel=.85, releaseSec=1.6, osc2Level=.7, osc2Detune=9, osc2Wave=1,
           transientType=0, transientLevel=.45, transientDecay=.3, transientTone=.35, volume=.62,
           fx={CHORUS_I: {'mix': .5, 'rateHz': .513, 'depth': .8}})],
        reverb=.42),
    vp.patch('LA32 · Strings', LA_CAT, 'LA sawtooth ensemble: two detuned partials, Juno II chorus', [
        la(sawLevel=1.0, cutoffHz=1500, resonanceQ=1.0, attackSec=.25, decaySec=1.5, sustainLevel=.9, releaseSec=.9,
           osc2Level=.85, osc2Detune=12, volume=.6, fx={CHORUS_II: {'mix': .5, 'rateHz': .863, 'depth': .8}})],
        reverb=.35),
    vp.patch('LA32 · Harpsichord', LA_CAT, 'very narrow LA pulse with a fast TVF decay', [
        la(sawLevel=0.0, pulseLevel=1.0, pulseWidth=.12, cutoffHz=3200, resonanceQ=2.0, filterEnvAmt=.4, filtDecaySec=.5,
           filtSustainLvl=.2, attackSec=.001, decaySec=1.6, sustainLevel=0.0, releaseSec=.4, osc2Level=.5, osc2Semitones=12,
           osc2Wave=1, transientType=4, transientLevel=.35, transientDecay=.02, volume=.62)],
        reverb=.2),
    vp.patch('LA32 · Vibraphone', LA_CAT, 'LA square mallet with a glass attack and tremolo', [
        la(sawLevel=0.0, pulseLevel=1.0, pulseWidth=.5, cutoffHz=500, resonanceQ=.5, attackSec=.001, decaySec=2.5,
           sustainLevel=0.0, releaseSec=1.2, transientType=5, transientLevel=.55, transientDecay=.25, transientTone=.6,
           volume=.75, fx={PHASER_I: {'mix': .2, 'rateHz': 4.5, 'depth': .3}})],
        reverb=.3),
    vp.patch('LA32 · Calliope', LA_CAT, 'soft LA square flute with breath noise', [
        la(sawLevel=0.0, pulseLevel=1.0, pulseWidth=.5, cutoffHz=220, resonanceQ=.5, filterKeyTrack=1.0, attackSec=.06,
           sustainLevel=1.0, releaseSec=.3, transientType=0, transientLevel=.4, transientDecay=.12, transientTone=.5,
           volume=.8)],
        lfos=[vp.lfo(rateHz=5.8, depth=.6, destPitch=8, delaySec=.25, fadeSec=.3, keySync=True)], reverb=.3),
    vp.patch('LA32 · Ring Bell', LA_CAT, 'ring-modulated LA partials, inharmonic bell', [
        la(sawLevel=0.0, pulseLevel=1.0, cutoffHz=2600, resonanceQ=1.5, attackSec=.001, decaySec=3.0, sustainLevel=0.0,
           releaseSec=2.0, osc2Level=1.0, osc2Semitones=19, osc2Detune=30, laRing=1.0, volume=.6)],
        reverb=.4, delay=(450, .3, .15)),
    vp.patch('LA32 · Brass Section', LA_CAT, 'two LA sawtooth partials, brassy TVF attack', [
        la(sawLevel=1.0, cutoffHz=900, resonanceQ=1.5, filterEnvAmt=.6, filtAttackSec=.04, filtDecaySec=.4, filtSustainLvl=.55,
           attackSec=.02, sustainLevel=.95, releaseSec=.2, osc2Level=.9, osc2Detune=-8, volume=.62, fx=LA_CHORUS)],
        reverb=.2),
    vp.patch('LA32 · Warm Pad', LA_CAT, 'dark LA square pad below the TVF middle point', [
        la(sawLevel=0.0, pulseLevel=1.0, pulseWidth=.5, cutoffHz=180, resonanceQ=.5, attackSec=.9, decaySec=2.0,
           sustainLevel=1.0, releaseSec=2.5, osc2Level=.8, osc2Detune=9, osc2Wave=1, volume=.85,
           fx={CHORUS_II: {'mix': .5, 'rateHz': .863, 'depth': .75}})],
        reverb=.4),
    vp.patch('LA32 · Digital Bass', LA_CAT, 'resonant LA pulse bass with a snappy TVF', [
        la(sawLevel=0.0, pulseLevel=1.0, pulseWidth=.3, octave=-1, cutoffHz=380, resonanceQ=9.0, filterEnvAmt=.6,
           filtDecaySec=.22, filtSustainLvl=.1, attackSec=.001, decaySec=.5, sustainLevel=.6, releaseSec=.08, volume=.8)],
        reverb=0.0, mono=(True, 0)),
]
assert len(BANK) == 40


def main():
    import sys
    gains = {}
    if '--gains' in sys.argv:
        gains = {int(k): float(v) for k, v in json.loads(Path(sys.argv[sys.argv.index('--gains') + 1]).read_text()).items()}
    catalog = []
    for offset, entry in enumerate(BANK):
        pid = FIRST_ID + offset
        params = copy.deepcopy(entry['params'])
        params['synthPresetId'] = pid
        # Level-match like build_vintage_presets.py: master first, then layer volumes.
        target = params['synthMasterVol'] * gains.get(pid, 1.0)
        params['synthMasterVol'] = round(min(1.2, max(.2, target)), 3)
        rest = target / params['synthMasterVol']
        if abs(rest - 1.0) > 1e-3:
            for lay in params['layers']:
                if lay['enabled']:
                    lay['volume'] = round(min(1.5, lay['volume'] * rest), 3)
        doc = {'factoryId': pid, 'name': entry['name'], 'category': entry['category'],
               'inspiration': entry['inspiration'], 'originalDesign': entry['category'] == LA_CAT, 'params': params}
        (FACTORY / f'{pid:03d}.json').write_text(json.dumps(doc, indent=1, ensure_ascii=False), encoding='utf8')
        catalog.append({'id': pid, 'name': entry['name'], 'category': entry['category']})
    text = CATALOG.read_text(encoding='utf8')
    m = re.search(r'export const SYNTH_FACTORY: readonly FactoryPatch\[\] = (\[.*?\]);\n', text, re.S)
    existing = [p for p in json.loads(m.group(1)) if p['id'] < FIRST_ID]
    text = text.replace(m.group(1), json.dumps(existing + catalog, ensure_ascii=False, separators=(',', ':')))
    m = re.search(r'export const SYNTH_FACTORY_CATEGORIES: readonly string\[\] = (\[.*?\]);', text)
    cats = [c for c in json.loads(m.group(1)) if c not in {DX7_CAT, LA_CAT}] + [DX7_CAT, LA_CAT]
    text = text.replace(m.group(1), json.dumps(cats, ensure_ascii=False))
    text = re.sub(r'^// .*\(tools/build_vintage_presets\.py\)\.$',
                  '// 384 generated factory patches + 48 vintage designs (tools/build_vintage_presets.py)'
                  ' + 24 DX7 voices and 16 LA32 designs (tools/build_engine_presets.py).', text, count=1, flags=re.M)
    CATALOG.write_text(text, encoding='utf8')
    print(f'wrote {len(BANK)} presets ({FIRST_ID}-{FIRST_ID + len(BANK) - 1}); catalogue now ends at {FIRST_ID + len(BANK) - 1}')


if __name__ == '__main__':
    main()
