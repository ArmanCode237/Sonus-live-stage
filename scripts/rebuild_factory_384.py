from pathlib import Path
import json,math,copy,shutil
r=Path(__file__).resolve().parents[1]
root=r/'presets/synth/factory'
template=json.loads((root/'001.json').read_text())
for p in root.glob('*.json'):p.unlink()
styles=[
 ('Juno-6','Poly Chorus','PADS'),('Juno-6','Punch Bass','BASS'),('Juno-6','Plucked Keys','KEYS'),
 ('Juno-60','Warm Strings','STRINGS'),('Juno-60','Arpeggiator','SAWS'),('Juno-60','Round Bass','BASS'),
 ('Juno-106','Chorus Pads','PADS'),('Juno-106','Electric Poly','POLYKEYS'),('Juno-106','Glass Pluck','KEYS'),
 ('Jupiter-8','Dual VCO','BRASS'),('Jupiter-8','Bright Brass','BRASS'),('Jupiter-8','Wide Lead','LEADS'),
 ('D-50','Digital Fantasia','PADS'),('D-50','Motion Bell','BELLS'),('D-50','Hybrid Atmosphere','STRINGS'),
 ('Prophet-5','Poly Brass','BRASS'),('Prophet-5','Sync Lead','LEADS'),('Prophet-5','Vintage Keys','POLYKEYS'),
 ('CS-80','Expressive Strings','STRINGS'),('CS-80','Sweeping Brass','BRASS'),('CS-80','Poly Voice','LEADS'),
 ('DX7','FM Electric Piano','KEYS'),('DX7','Crystal Bells','BELLS'),('DX7','FM Bass','BASS')]
variants=['Amber','Velvet','Aurora','Prism','Solar','Midnight','Cobalt','Opaline','Nova','Copper','Horizon','Glacier','Pulse','Afterglow','Lunar','Eclipse']

for family,(instrument,style,category) in enumerate(styles):
 model=family//3;role=family%3
 for variation,vname in enumerate(variants):
  idx=family*16+variation+1
  params=copy.deepcopy(template['params'])
  params.update(synthEnabled=True,synthPresetId=idx,synthMasterVol=round(.74+.025*((variation+family)%5),4),synthMacroCutoff=1.0,synthWheelGlobalCutoff=0.0,synthWheelGlobalAmpDb=0.0)
  active=1+(variation%4)
  layers=[];lfos=[]
  for layer in range(4):
   c=copy.deepcopy(template['params']['layers'][layer])
   c['enabled']=layer<active
   c['volume']=round((.73-.07*layer)*(.9+.025*(variation%5)),4)
   c['pan']=round((layer-1.5)*(.13+(variation%3)*.018),4) if active>1 else 0
   c['octave']=(-1 if (role==1 and layer==0) else 0)+(1 if layer==3 and model in (3,4,7) and variation%2 else 0)
   c['detuneCents']=round((-6+layer*4)+((variation%7)-3)*.85,3)
   c['semitones']=7 if layer==2 and model in (3,5,6) and variation%3==0 else 0
   # Familiar sound families, all generated from the SONUS engine rather than factory ROM/PCM copies.
   baseline_wave=[8,8,8,9,6,8,8,8][model]
   wave_choice=[8,1,8,4,0,2,6,3,9,12,7,11,10,6,15,16,17,13,19,20,8][(variation*3+layer+family)%21]
   c['waveType']=baseline_wave if layer==0 and variation%4!=3 else wave_choice
   if model==3:c['waveType']=[9,9,7,4][layer]
   if model==4:c['waveType']=[6,13,6,17][layer]
   if model==5:c['waveType']=[8,7,1,9][layer]
   if model==6:c['waveType']=[12,8,9,6][layer]
   if model==7:c['waveType']=[10,15,10,6][layer]
   c['synthesisType']=2 if model==4 else (1 if model==7 else 0)
   if model in (0,1,2,3,5,6) and layer>0 and variation%6==5:c['synthesisType']=2
   if model==4 and layer==2 and variation%5==3:c['synthesisType']=1
   if model==7 and layer==3 and variation%4==2:c['synthesisType']=0
   c['fmIndex']=round((1.2+variation*.17+layer*.29) if model==7 else (.9+variation*.08),3)
   c['fmRatio']=[.5,1,2,3,4,5,6,8,12][(family+variation+layer)%9]
   c['wavetableIdx']=(family*5+variation*3+layer*7)%24
   c['wavetableMorph']=round(.12+.06*((variation+layer)%9),4)
   c['waveType']=max(0,min(20,int(c['waveType'])))
   # Envelope role based on ensemble, bass or pluck.
   is_pad=category in ('PADS','STRINGS')
   is_pluck=role==2 and category in ('KEYS','BELLS')
   is_lead=category=='LEADS'
   c['attackSec']=round((.16+variation*.012+layer*.06) if is_pad else (.004+layer*.004 if is_pluck else .018+layer*.007),4)
   c['decaySec']=round((1.2+.11*variation+.12*layer) if is_pad else (.25+.065*variation+.06*layer),4)
   c['sustainLevel']=round((.62+.018*((variation+layer)%12)) if is_pad else (.05 if is_pluck else .62+.018*(variation%9)),4)
   c['releaseSec']=round((1.5+variation*.12+layer*.18) if is_pad else (.21+.047*variation+.07*layer),4)
   c['filterType']=[0,0,0,0,1,2,3][(variation+family+layer)%7]
   c['cutoffHz']=round(min(17000,max(190,([3900,1600,7100][role]+variation*210+layer*970)*(1.35 if model in (3,7) else 1))),3)
   c['resonanceQ']=round(.78+.08*((variation+layer)%9),4)
   c['filterEnvAmt']=round((.22 if category in ('BRASS','BASS','KEYS') else -.08)+((variation%5)-2)*.065,4)
   c['filtAttackSec']=round(.007 if is_pluck else .08 if is_pad else .012,4)
   c['filtDecaySec']=round(.45+variation*.06,4)
   c['filtSustainLvl']=round(.3+.04*((variation+layer)%8),4)
   c['filtReleaseSec']=round(.32+.08*layer+(.6 if is_pad else 0),4)
   c['sawLevel']=round(.38+.035*((variation+layer)%11),4)
   c['pulseLevel']=round(.19+.028*((family+layer)%10),4)
   c['pulseWidth']=round(.25+.027*((family+variation+layer)%15),4)
   c['pwmDepth']=round(.05+.017*((variation+layer)%13),4)
   c['pwmRateHz']=round(.12+.06*((family+variation+layer)%15),4)
   c['subLevel']=round(.3+.025*(variation%5),4) if role==1 else .05+layer*.023
   c['noiseLevel']=.006+.006*(variation%5)
   c['osc2Level']=round(.22+.043*((variation+layer)%8),4) if model in (3,5,6) else .12+layer*.04
   c['osc2Detune']=round(4.2+variation*.5+layer*1.6,4)
   c['osc2Semitones']=12 if model in (3,4) and layer%2 else 0
   c['osc2Wave']=(variation+layer)%3
   c['highpassHz']=20+12*((variation+layer)%8)
   c['drive']=round(.04+.035*((variation+family)%9),4)
   c['unisonVoices']=min(4,1+((variation+layer)//3)%4) if is_pad else (2 if variation%4==3 else 1)
   c['unisonDetune']=round(5.0+1.6*(variation%9),3)
   c['unisonSpread']=round(.2+.1*((variation+layer)%8),3)
   c['voiceMode']=1 if is_lead and layer==0 else 0
   c['portamentoEnabled']=is_lead and layer==0
   c['portamento']=round(.055+.02*(variation%8),3) if is_lead else 0
   c['glideMode']=1
   c['wheelCutoffOctaves']=round(((-1 if layer%2 else 1)*(.22+.13*((variation+layer)%6))),3)
   c['wheelAmpDb']=round(-3.4+1.1*((variation+layer)%7),3)
   c['wheelVibratoCents']=round(9+3*((variation+layer)%8),3)
   c['wheelVibratoRateHz']=round(4.6+.21*(variation%10),3)
   c['wheelWtMorph']=round(.12+.065*((variation+layer)%9),3) if c['synthesisType']==2 else 0
   c['keyLow']=0 if layer==0 or not variation%5==4 else 48
   c['keyHigh']=127 if layer!=3 or variation%5!=4 else 85
   c['keyTranspose']=0
   c['seqEnabled']=category=='SAWS' and (variation%3==0) and layer==0
   c['seqBpm']=int(80+variation*5)
   c['seqSteps']=8+variation%9
   c['seqGate']=round(.35+.05*(variation%8),3)
   c['seqNotes']=[(0,3,7,12,7,3,0,-5)[(step+variation+layer)%8] for step in range(16)]
   for fx in range(5):
    eff=c['insertFx'][fx]
    eff['enabled']=((fx==0 and model in (0,1,2)) or (fx==1 and is_pad and layer%2==0) or (fx==2 and is_lead and variation%2==1) or (fx==3 and model in (4,6) and variation%3==1) or (fx==4 and (is_pad or category in ('KEYS','BELLS')) and variation%3!=2)) and layer<active
    eff['rateHz']=round(.15+(((family+variation+layer+fx)%17)*.21),4)
    eff['depth']=round(.2+.053*((variation+fx)%11),3)
    eff['mix']=round(.17+.027*((variation+layer+fx)%11),3)
    eff['feedback']=round(.085+.029*((variation+fx)%9),4)
    eff['timeMs']=int(120+((variation*29+layer*37)%580))
    eff['syncEnabled']=fx==4 and variation%4!=2
    eff['syncDivision']=[5,6,7,8,9,13,14,15][(variation+layer)%8]
   layers.append(c)
   on=variation%4!=0 and layer<active
   lfos.append(dict(enabled=on,waveType=(variation+layer)%4,
    rateHz=round(.15+.18*((variation*2+layer+family)%18),4),depth=round(.13+.045*((variation+layer)%13),4),
    delaySec=0.0,fadeSec=0.0,
    destPitch=(6+3*((variation+layer)%12)) if model in (3,6) else (2+2*((variation+layer)%6)),
    destCutoff=int((-1 if layer%2 else 1)*(90+95*((variation+layer)%12))),
    destVolume=round(.02+.013*((variation+layer)%10),4),destPan=round(.04+.022*((variation+layer)%12),4),destPhase=0.0))
  params['layers']=layers
  params['lfos']=lfos
  # mono LFO1 alias is in full snapshots, factory-only JSON uses `lfos` directly.
  record={'factoryId':idx,'name':f'{vname} {instrument} {style}',
          'category':category,'inspiration':instrument,
          'params':params}
  (root/f'{idx:03d}.json').write_text(json.dumps(record,ensure_ascii=False,separators=(',',':'))+'\n')
assert len(list(root.glob('*.json')))==384
assert all(json.load(open(root/f'{i:03d}.json'))['factoryId']==i for i in range(1,385))
print('GENERATED 384 FACTORY PRESETS, 24 FAMILIES, 16 VARIATIONS, 1-4 LAYERS, LFO+INSERT FX+WHEEL')
