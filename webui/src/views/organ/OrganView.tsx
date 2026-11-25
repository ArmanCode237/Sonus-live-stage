// Organ: model, nine drawbars, vibrato/chorus, percussion, rotary and presets.
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import type { NamedPreset } from '../../api/types';
import { DRAWBAR_FOOTAGES, ORGAN_MODELS, ORGAN_PERC_HARMONICS, ORGAN_VIBRATO_MODES } from '../../data/constants';
import { engine } from '../../store/engine';
import { useEngine } from '../../store/hooks';
import { runAction } from '../../store/notify';
import { ParamKnob } from '../../ui/Knob';
import { Btn, Empty, Lcd, Panel, ParamSegmented, ParamToggle, Row } from '../../ui/controls';
import { Drawbar } from '../shared/Drawbar';
import { DirtyTag } from '../../ui/DirtyTag';
import { ModuleTabs } from '../shared/ModuleTabs';
import { OrganSoundBrowser } from '../../browser/ModuleBrowsers';

function DrawbarPresets() {
  const setAll = (values: number[]) => values.forEach((v, i) => engine.set(`drawbar${i}`, v / 8));
  const registrations: [string, number[]][] = [
    ['FULL', [8, 8, 8, 8, 8, 8, 8, 8, 8]], ['JAZZ', [8, 8, 8, 0, 0, 0, 0, 0, 0]], ['GOSPEL', [8, 8, 8, 8, 0, 0, 0, 0, 8]],
    ['ROCK', [8, 8, 8, 8, 0, 0, 0, 0, 0]], ['BALLAD', [8, 3, 8, 0, 0, 0, 0, 0, 0]], ['FLUTE', [0, 0, 8, 4, 0, 0, 0, 0, 0]],
  ];
  return (
    <Row wrap className="registrations">
      {registrations.map(([name, values]) => <Btn key={name} small onClick={() => setAll(values)}>{name}</Btn>)}
    </Row>
  );
}

function ModelPanel() {
  const model = useEngine(s => s?.params?.organModel ?? 0);
  const [factory, setFactory] = useState<{ factoryId: number; name: string }[]>([]);
  const [browsing, setBrowsing] = useState(false);
  useEffect(() => { api.organFactory().then(setFactory).catch(() => {}); }, []);
  return (
    <Panel title="ORGAN" power={{ id: 'organEnabled', label: 'Organ' }} className="panel--module organ-model"
      actions={<><DirtyTag module={2} /><Btn small variant="browse" onClick={() => setBrowsing(true)} title="Buscar y organizar presets de fábrica y de usuario">BROWSER</Btn></>}>
      {browsing && <OrganSoundBrowser onClose={() => setBrowsing(false)} />}
      <Lcd className="patch-lcd">
        <div className="patch-lcd__top"><span>MODEL {String(Math.round(model) + 1).padStart(2, '0')} / {ORGAN_MODELS.length}</span></div>
        <div className="patch-lcd__name">{ORGAN_MODELS[Math.round(model)] ?? '—'}</div>
      </Lcd>
      <ParamSegmented id="organModel" label="Modelo de órgano" options={ORGAN_MODELS} wrap />
      {factory.length > 0 && (
        <>
          <span className="subhead">FACTORY</span>
          <Row wrap>
            {factory.map(f => (
              <Btn key={f.factoryId} small onClick={() => runAction(() => api.loadOrganFactory(f.factoryId), f.name, true)}>{f.name}</Btn>
            ))}
          </Row>
        </>
      )}
      <div className="knobs">
        <ParamKnob id="organVolume" label="LEVEL" /><ParamKnob id="organDrive" label="DRIVE" />
        <ParamKnob id="organPan" label="PAN" size="sm" /><ParamKnob id="organTune" label="TUNE" size="sm" />
      </div>
    </Panel>
  );
}

function Presets() {
  const [presets, setPresets] = useState<NamedPreset[]>([]);
  const [name, setName] = useState('');
  useEffect(() => { api.organPresets().then(setPresets).catch(() => {}); }, []);
  const save = async () => { const r = await runAction(() => api.saveOrganPreset(name.trim()), `«${name.trim()}» guardado`); if (r) { setPresets(r.presets); setName(''); } };
  const remove = async (n: string) => { const r = await runAction(() => api.deleteOrganPreset(n), `«${n}» eliminado`); if (r) setPresets(r.presets); };
  return (
    <Panel title="USER PRESETS" className="organ-presets">
      <form className="row" onSubmit={e => { e.preventDefault(); if (name.trim()) void save(); }}>
        <input className="input grow" placeholder="Nombre…" maxLength={64} value={name} onChange={e => setName(e.target.value)} aria-label="Nombre del preset de órgano" />
        <Btn variant="primary" type="submit" disabled={!name.trim()}>GUARDAR</Btn>
      </form>
      <ul className="list">
        {presets.map(p => (
          <li key={p.name} className="list__row">
            <button type="button" className="list__main" onClick={() => runAction(() => api.loadOrganPreset(p.name), `«${p.name}»`, true)}>{p.name}</button>
            <button type="button" className="icon-btn" aria-label={`Eliminar ${p.name}`} onClick={() => remove(p.name)}>×</button>
          </li>
        ))}
        {presets.length === 0 && <Empty>Sin presets de usuario.</Empty>}
      </ul>
    </Panel>
  );
}

export default function OrganView() {
  return (
    <ModuleTabs module={2}>
      <div className="view view--organ">
        <div className="stack">
          <ModelPanel />
          <Presets />
        </div>
        <Panel title="DRAWBARS" className="drawbars-panel" actions={<DrawbarPresets />}>
          <div className="drawbars">{DRAWBAR_FOOTAGES.map((_, i) => <Drawbar key={i} index={i} />)}</div>
        </Panel>
        <div className="stack">
          <Panel title="VIBRATO / CHORUS" className="organ-vibrato">
            <ParamSegmented id="organVibratoMode" label="Vibrato / chorus" options={ORGAN_VIBRATO_MODES} />
            <div className="knobs"><ParamKnob id="organVibratoDepth" label="DEPTH" size="sm" /><ParamKnob id="organVibratoRate" label="RATE" size="sm" /></div>
          </Panel>
          <Panel title="PERCUSSION" power={{ id: 'organPercEnabled', label: 'Percusión' }} className="organ-perc">
            <ParamSegmented id="organPercHarmonic" label="Armónico de percusión" options={ORGAN_PERC_HARMONICS.map(h => h.label)} values={ORGAN_PERC_HARMONICS.map(h => h.value)} />
            <div className="knobs"><ParamKnob id="organPercLevel" label="LEVEL" size="sm" /><ParamKnob id="organPercDecaySec" label="DECAY" size="sm" /></div>
          </Panel>
          <Panel title="ROTARY" className="organ-rotary" actions={<ParamToggle id="organRotaryFast" label="FAST" small />}>
            <div className="knobs">
              <ParamKnob id="organRotaryMix" label="MIX" size="sm" /><ParamKnob id="organRotaryFastRate" label="FAST RATE" size="sm" />
              <ParamKnob id="organRotaryRamp" label="RAMP" size="sm" /><ParamKnob id="organRotaryStereoWidth" label="WIDTH" size="sm" />
            </div>
          </Panel>
          <Panel title="ENSEMBLE CHORUS" power={{ id: 'organChorusEnabled', label: 'Chorus de órgano' }} className="organ-chorus">
            <div className="knobs">
              <ParamKnob id="organChorusRate" label="RATE" size="sm" /><ParamKnob id="organChorusDepth" label="DEPTH" size="sm" />
              <ParamKnob id="organChorusMix" label="MIX" size="sm" /><ParamKnob id="organChorusWidth" label="WIDTH" size="sm" />
              <ParamKnob id="organChorusTone" label="TONE" size="sm" /><ParamKnob id="organChorusVoices" label="VOICES" size="sm" />
            </div>
          </Panel>
        </div>
      </div>
    </ModuleTabs>
  );
}
