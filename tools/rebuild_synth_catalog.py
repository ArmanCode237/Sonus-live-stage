"""Deterministic original patch design; does not contain third-party samples or factory data."""
from pathlib import Path
import json, copy, math
root=Path(__file__).resolve().parents[1]
factory=root/'presets/synth/factory'
base=json.loads((factory/'001.json').read_text())['params']
# A family has a distinct synthesis architecture, spectral source, contour,
# expressive response and voice count. Inspiration is descriptive, not an emulation promise.
families=[
 # name, category, inspiration, synthesis, waveform, wavetable, cutoff, attack, decay, sustain, release, layers, special
 ('Jupiter Brass Ensemble','BRASS','1980s dual-VCO analog brass',0,9,14,5000,.012,.45,.76,.57,2,'brass'),
 ('Prophet Poly Brass','BRASS','vintage polyphonic brass stabs',0,9,28,3200,.025,.30,.71,.42,2,'brass'),
 ('CS-80 Expressive Lead','LEADS','expressive Yamaha CS-80 style brass leads',0,16,15,4400,.016,.47,.86,.50,2,'lead'),
 ('Minimoog Ladder Bass','BASS','1970s monophonic ladder-filter bass',0,24,27,1100,.004,.27,.64,.22,1,'bass'),
 ('303 Acid Sequence','SAWS','resonant acid synthesizer sequencing',0,0,35,960,.003,.18,.27,.16,1,'acid'),
 ('Juno Chorus Velvet','PADS','single-DCO ensemble chorus pads',0,8,25,3600,.33,1.4,.87,2.4,2,'pad'),
 ('Prophet Warm Strings','STRINGS','analog string ensemble pads',0,21,34,2400,.42,1.1,.82,2.7,3,'strings'),
 ('OBX Synth Brass','BRASS','wide SEM-style brass synth textures',0,9,28,4300,.012,.39,.73,.55,2,'brass'),
 ('DX7 Crystal EP','KEYS','classic 2-operator FM electric keys',1,10,26,9300,.003,1.5,.22,1.8,2,'ep'),
 ('DX7 Glass Bells','BELLS','digital FM bell partials',1,10,26,11000,.002,3.2,.07,3.1,2,'bell'),
 ('D50 Digital Fantasia','PADS','layered digital-analog motion pads',2,6,29,8400,.36,1.5,.8,3.0,3,'fantasia'),
 ('PPG Crystal Sweep','PADS','early wavetable morphing textures',2,6,31,8700,.13,1.2,.78,2.2,2,'wavetable'),
 ('Solina Ensemble Strings','STRINGS','vintage string-machine ensemble',0,21,25,5400,.25,1.1,.84,2.5,3,'strings'),
 ('Mellotron Air Choir','STRINGS','tape-choir-style harmonic sound',2,6,29,3900,.13,1.1,.68,1.4,2,'choir'),
 ('Synclavier Metal Mallet','BELLS','additive digital metallic mallets',0,22,32,12400,.002,2.0,.1,1.7,2,'bell'),
 ('House Organ Pluck','KEYS','early house music synth-organ plucks',0,23,4,5700,.003,.38,.24,.36,2,'pluck'),
 ('Vintage Mono Solo','LEADS','1970s expressive analog monophonic solo',0,16,6,3200,.010,.25,.86,.47,1,'lead'),
 ('Synthwave Neon Lead','LEADS','80s synthwave lead arpeggio voices',0,27,20,7600,.006,.35,.78,.48,2,'lead'),
 ('Reese Detune Bass','BASS','wide detuned electronic bass',0,24,27,2000,.005,.35,.72,.36,2,'bass'),
 ('Chiptune Pulse Arp','SAWS','8-bit pulse and retro game synths',0,11,25,10700,.002,.22,.49,.16,1,'chip'),
 ('FM Plucked Clav','KEYS','bright FM plucked digital keys',1,10,33,8100,.002,.55,.20,.43,2,'ep'),
 ('Evolving Vox Texture','PADS','sweeping voice-formant wavetable',2,6,24,4500,.25,1.7,.77,3.2,3,'choir'),
 ('French Touch Synth Stab','POLYKEYS','compressed funky synth stabs',0,9,28,3700,.002,.18,.28,.35,2,'pluck'),
 ('Ambient Glass Atmosphere','PADS','new-age digital layered atmosphere',2,6,30,8500,.65,2.2,.80,4.1,3,'fantasia')
]
variants=['Classic','Warm','Bright','Dark','Soft Touch','Wide Stereo','Intimate',
          'Glassy','Deep','Airy','Hollow','Motion','Slow Bloom','Short Attack','Performance','Solo Cut']
assert len(families)==24 and len(variants)==16

def clamp(x,lo,hi):return max(lo,min(hi,x))
def round_params(obj):
 for k,v in list(obj.items()):
  if isinstance(v,float):obj[k]=round(v,5)
 return obj

new=[]
for family,(title,category,description,typ,wave,wt,cut,attack,decay,sustain,release,nlayers,character) in enumerate(families):
 for vid,vname in enumerate(variants):
  pid=family*16+vid+1
  p=copy.deepcopy(base)
  p.update(synthPresetId=pid,synthEnabled=True,synthMasterVol=.78,
           synthMacroCutoff=1,synthWheelGlobalCutoff=0,synthWheelGlobalAmpDb=0,
           synthVoiceMaster=False,synthVoiceMode=0,synthPortamentoEnabled=False,synthPortamento=0.)
  # Macro variation is musically intentional, not just 16 copies with new names.
  spectral=[1.,.67,1.6,.46,.8,1.14,.73,1.42,.58,1.23,.64,1.09,.88,1.56,1.25,.95][vid]
  attackMult=[1,1.10,.80,1.2,1.5,1,1.22,.55,1.10,1.7,1.12,1.45,3.,.55,.9,.86][vid]
  releaseMult=[1.,1.25,.95,1.4,1.3,1.7,.82,1.12,1.05,1.8,1.3,1.5,2.2,.5,.9,.67][vid]
  fineLayers=nlayers if vid!=15 else 1
  if character in ('bell','pluck','chip','acid') and vid in (4,6,8):fineLayers=1
  for l,c in enumerate(p['layers']):
   # Reset all writable values to a deliberate, bounded state.
   c.update(enabled=l<fineLayers,synthesisType=typ,waveType=wave,
      volume=round(clamp((.80-.065*l)/math.sqrt(fineLayers),.27,.9),4),
      pan=((-1 if l%2==0 else 1)*(.09*l+.16*(vid==5))) if fineLayers>1 else 0.,
      octave=0, semitones=0,detuneCents=round(((-1)**l)*(2.2+vid%7*1.8),3),
      cutoffHz=round(clamp(cut*spectral*(1+l*.16),90,19000),2),
      resonanceQ=round(clamp(.77+.14*(vid%4)+(.8 if character=='acid' else 0),.55,7),3),
      attackSec=round(clamp(attack*attackMult*(1+.3*l),.001,8),4),
      decaySec=round(clamp(decay*(1+.16*(vid%5)),.02,10),4),
      sustainLevel=round(clamp(sustain+(vid%4-1)*.045,.035,1),4),
      releaseSec=round(clamp(release*releaseMult*(1+.16*l),.02,10),4),
      unisonVoices=(2 if character in ('pad','strings','fantasia','wavetable') and l==0 and vid in (5,9,11,12) else 1),
      unisonDetune=8+vid%7*2.5,unisonSpread=.38+(.28 if vid==5 else .08),
      phaseOffset=0.,keyTrack=1.,velToAmp=1.,velToCutoff=.15+(vid%4)*.11,
      wavetableIdx=(wt+(vid//4)+l*2)%36,wavetableMorph=.16+(vid%5)*.13,
      fmIndex=round(clamp((1.9+(vid%4)*1.2+(l*.3)),.3,8),3),
      fmRatio=round(([1.,2.,3.,.5,4.,1.5,5.,.75][vid%8]),3),
      filterType=0,filterEnvAmt=(.52 if character in ('brass','pluck','acid') else .22),
      filterKeyTrack=.40,filtAttackSec=.01,filtDecaySec=.32,filtSustainLvl=.36,
      filtReleaseSec=.34,highpassHz=20,drive=(.20 if character in ('lead','acid','bass') else 0),
      sawLevel=.72,pulseLevel=.27,pulseWidth=.28+(vid%6)*.07,
      pwmDepth=.035+(.07 if vid in (5,11) else 0),pwmRateHz=.3+.13*(vid%7),
      subLevel=(.42 if character in ('bass','acid') else .12),
      noiseLevel=(.03 if character in ('strings','choir','fantasia') else .006),
      osc2Level=(.44 if typ==0 else .15),osc2Detune=4.+vid%9*2,
      osc2Semitones=(12 if character=='bell' and l>0 else 0),osc2Wave=(l+vid)%3,
      voiceMode=1 if character in ('lead','bass','acid') else 0,
      portamentoEnabled=character in ('lead','acid') and vid not in (4,13),
      portamento=(.09+.025*(vid%6)) if character in ('lead','acid') else 0.,
      glideMode=1,slide=0,wheelMode=2,wheelVibratoCents=25.+(vid%5)*5,
      wheelVibratoRateHz=4.6+.17*(vid%8),wheelCutoffOctaves=(1.+.18*(vid%4)) if typ!=1 else 0,
      wheelAmpDb=0,wheelWtMorph=.3 if typ==2 else 0,
      seqEnabled=False,seqBpm=100+5*(vid%6),seqGate=.8,seqSteps=16,
      keyLow=0,keyHigh=127,keyTranspose=0)
   if character in ('ep','bell','pluck','chip'):
    c['sustainLevel']=round(.07+.1*(vid%3),3)
    c['releaseSec']=round(clamp(releaseMult*.6+.2*l,.12,5),3)
   if character=='acid':
    c['cutoffHz']=round(clamp(550+vid*175,400,4200),3)
    c['resonanceQ']=round(2.+vid*.25,3)
    c['filterEnvAmt']=.77
   if character in ('pad','strings','fantasia','choir'):
    c['sustainLevel']=.74
   if character=='chip':c['waveType']=11 if vid%2 else 1
   if character=='ep':c['waveType']=10
   if character=='bell':c['waveType']=25 if typ==0 else 10
   if character=='lead' and vid==7:c['waveType']=25
   if character=='strings' and vid in (3,9):c['waveType']=28
   if character=='fantasia' and l==2:c['waveType']=23;c['synthesisType']=0
   if character=='reese' and l==1:c['waveType']=21
   # No rhythmic FX by default; long tails only on intended atmospheric sounds.
   for slot,fx in enumerate(c['insertFx']):
    fx.update(enabled=False,rateHz=.36+.09*(vid%7),depth=.3,mix=.23,feedback=.18,timeMs=280,
              syncEnabled=False,syncDivision=5)
   if character in ('pad','strings','fantasia','wavetable','choir'):
    c['insertFx'][0].update(enabled=True,rateHz=.28+.05*(vid%6),depth=.45,mix=.25)
   if character in ('lead','ep','pluck','bell') and vid in (5,9,12):
    c['insertFx'][4].update(enabled=True,timeMs=210+25*(vid%6),mix=.17,feedback=.25)
   if l==1 and character in ('brass','lead','strings'):c['detuneCents']=-c['detuneCents']
   if l>=fineLayers:c['enabled']=False
   round_params(c)
  for l,lfo in enumerate(p['lfos']):
   on=l<fineLayers and character in ('pad','strings','fantasia','choir','wavetable','lead')
   lfo.update(enabled=on,waveType=0 if vid%3 else 1,rateHz=round(.18+.075*(vid%7),3),
       depth=.25+(vid%4)*.10,delaySec=.1,fadeSec=.5,
       destPitch=(7 if character=='lead' else 1),
       destCutoff=(240 if on else 0),destVolume=0.,destPan=.06 if l%2 else -.06,destPhase=0.)
  # Key groups with very high CPU requirements never use four independent 8x unison layers.
  name=f'{title} · {vname}'
  new.append({'factoryId':pid,'name':name,'category':category,
              'inspiration':description,'originalDesign':True,'params':p})
assert len(new)==384
# The old release also included 1.json..99.json, obsolete duplicates.
for stale in factory.glob('*.json'):
    if stale.stem.isdigit() and len(stale.stem)<3: stale.unlink()
for item in new:
 path=factory/f'{item["factoryId"]:03}.json'
 path.write_text(json.dumps(item,ensure_ascii=False,separators=(',',':'))+'\n')
# The React browser is the only factory-catalog consumer. Generate its typed
# catalogue directly instead of reviving the removed ui/js/ manifest.
catalogue=[{'id':x['factoryId'],'name':x['name'],'category':x['category']} for x in new]
categories=list(dict.fromkeys(x['category'] for x in new))
output=root/'webui/src/data/synthFactory.ts'
output.write_text(
    '// Generated by tools/rebuild_synth_catalog.py (384 original factory patches).\n'
    'export interface FactoryPatch { id: number; name: string; category: string }\n'
    'export const SYNTH_FACTORY: readonly FactoryPatch[] = '+
      json.dumps(catalogue,ensure_ascii=False,separators=(',',':'))+';\n'
    'export const SYNTH_FACTORY_CATEGORIES: readonly string[] = '+
      json.dumps(categories,ensure_ascii=False,separators=(',',':'))+';\n', encoding='utf-8'
)
print('factory catalog',len(new),'first',new[0]['name'],'last',new[-1]['name'])
