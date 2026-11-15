// Per-layer sample voice editor (pianoSample{n}* parameters) and layer Sounds.
import { useCallback, useEffect, useState } from 'react';
import { api } from '../../api/client';
import type { SoundSummary } from '../../api/types';
import { PIANO_LAYER_COUNT } from '../../data/constants';
import { runAction } from '../../store/notify';
import { ParamKnob } from '../../ui/Knob';
import { Btn, Empty, Field, Panel, ParamSegmented, ParamToggle, Row, Segmented } from '../../ui/controls';
import { useEngine, useParam } from '../../store/hooks';
import { Modal } from '../../ui/Modal';
import { InsertChain } from '../shared/InsertChain';
import { EqScreen } from '../../ui/EqScreen';
import type { EqModel } from '../../ui/eqResponse';

const TABS = ['EDIT', 'INSERTS', 'SOUNDS'] as const;

function EnvFilter({ p }: { p: string }) {
  return (
    <>
      <fieldset className="group"><legend>AMPLITUDE</legend>
        <div className="knobs">
          <ParamKnob id={`${p}Atk`} label="ATTACK" /><ParamKnob id={`${p}Dec`} label="DECAY" />
          <ParamKnob id={`${p}Sus`} label="SUSTAIN" /><ParamKnob id={`${p}Rel`} label="RELEASE" />
          <ParamKnob id={`${p}Hold`} label="LOOP HOLD" />
        </div>
      </fieldset>
      <fieldset className="group"><legend>TONE</legend>
        <div className="knobs">
          <ParamKnob id={`${p}Cut`} label="CUTOFF" /><ParamKnob id={`${p}Res`} label="RESO" />
          <ParamKnob id={`${p}Gain`} label="GAIN" /><ParamKnob id={`${p}Tune`} label="FINE" />
          <ParamKnob id={`${p}Pan`} label="PAN" />
        </div>
      </fieldset>
    </>
  );
}

function Play({ p }: { p: string }) {
  return (
    <>
      <fieldset className="group"><legend>VOICE · GLIDE</legend>
        <Row wrap>
          <ParamToggle id={`${p}Mono`} label="MONO" small /><ParamToggle id={`${p}Legato`} label="LEGATO" small />
          <ParamToggle id={`${p}PortOn`} label="GLIDE" small />
          <ParamSegmented id={`${p}PortMode`} label="Modo de glide" options={['TIME', 'RATE']} />
        </Row>
        <div className="knobs"><ParamKnob id={`${p}PortTime`} label="GLIDE TIME" /></div>
      </fieldset>
      <fieldset className="group"><legend>MOD WHEEL</legend>
        <Row wrap>
          <ParamToggle id={`${p}WheelCutoffOn`} label="CUTOFF" small /><ParamToggle id={`${p}WheelCutoffInvert`} label="INVERT" small />
          <ParamToggle id={`${p}WheelAmpOn`} label="AMP" small /><ParamToggle id={`${p}WheelVibratoOn`} label="VIBRATO" small />
        </Row>
        <div className="knobs">
          <ParamKnob id={`${p}WheelCutoff`} label="CUTOFF" size="sm" /><ParamKnob id={`${p}WheelAmp`} label="AMP" size="sm" />
          <ParamKnob id={`${p}WheelVibratoDepth`} label="VIB DEPTH" size="sm" /><ParamKnob id={`${p}WheelVibratoRate`} label="VIB RATE" size="sm" />
        </div>
      </fieldset>
    </>
  );
}

/** Live Sound EQ of a layer (values in Hz / dB, as the engine reports them). */
function useSoundEqModel(p: string): EqModel {
  const v = (id: string) => useParam(`${p}${id}`).value;
  const enabled = (v('Eq0') ?? 0) >= 0.5;
  const hpHz = v('EqHP'), lpHz = v('EqLP'), hpSlope = v('Eq3'), lpSlope = v('Eq4');
  const bands = [0, 1, 2].map(b => ({ freqHz: v(`EqF${b}`) ?? 1000, q: v(`EqQ${b}`) ?? 1, gainDb: v(`EqG${b}`) ?? 0 }));
  return { enabled, hpHz, lpHz, hpSlope: hpSlope === undefined ? undefined : Math.round(hpSlope), lpSlope: lpSlope === undefined ? undefined : Math.round(lpSlope), bands };
}

function Eq({ p }: { p: string }) {
  const model = useSoundEqModel(p);
  return (
    <fieldset className="group group--wide"><legend>SOUND EQ</legend>
      <EqScreen model={model} title="SOUND EQ" className="eq-screen--sound" />
      <div className="eq-layout">
      <div className="eq-layout__filters">
        <Row wrap>
          <ParamToggle id={`${p}Eq0`} label="EQ ON" small />
          <ParamSegmented id={`${p}Eq3`} label="Pendiente HP" options={['12', '24', '36', '48']} values={[12, 24, 36, 48]} />
          <ParamSegmented id={`${p}Eq4`} label="Pendiente LP" options={['12', '24', '36', '48']} values={[12, 24, 36, 48]} />
        </Row>
        <div className="knobs"><ParamKnob id={`${p}EqHP`} label="HIGH PASS" size="sm" /><ParamKnob id={`${p}EqLP`} label="LOW PASS" size="sm" /></div>
      </div>
        <div className="eq-bands">
          {['LOW', 'MID', 'HIGH'].map((name, b) => (
            <div key={name} className="eq-band"><span className="eq-band__name">{name}</span>
              <ParamKnob id={`${p}EqG${b}`} label="GAIN" size="sm" /><ParamKnob id={`${p}EqF${b}`} label="FREQ" size="sm" />
              <ParamKnob id={`${p}EqQ${b}`} label="Q" size="sm" />
            </div>
          ))}
        </div>
      </div>
    </fieldset>
  );
}

function Sounds({ layer }: { layer: number }) {
  const [sounds, setSounds] = useState<SoundSummary[] | null>(null);
  const [name, setName] = useState('');
  const [error, setError] = useState('');
  const reload = useCallback(() => {
    api.sounds('piano').then(r => setSounds(r.sounds.filter(s => s.scope === 'layer'))).catch(e => setError((e as Error).message));
  }, []);
  useEffect(reload, [reload]);
  const save = async () => {
    if (!name.trim()) return;
    if (await runAction(() => api.saveSound({ module: 'piano', name: name.trim(), layer }), `Sound «${name.trim()}» guardado`)) { setName(''); reload(); }
  };
  const remove = async (s: SoundSummary) => { if (await runAction(() => api.deleteSound(s.id), `«${s.name}» eliminado`)) reload(); };
  return (
    <div className="sounds">
      <form className="row" onSubmit={e => { e.preventDefault(); void save(); }}>
        <input className="input" placeholder={`Guardar L${layer + 1} como Sound…`} maxLength={64} value={name} onChange={e => setName(e.target.value)} aria-label="Nombre del Sound" />
        <Btn variant="primary" type="submit" disabled={!name.trim()}>GUARDAR</Btn>
      </form>
      {error && <Empty>{error}</Empty>}
      {sounds && sounds.length === 0 && <Empty>Aún no hay Sounds de capa.</Empty>}
      <ul className="list">
        {sounds?.map(s => (
          <li key={s.id} className="list__row">
            <button type="button" className="list__main" onClick={() => runAction(() => api.loadSound(s.id, layer), `«${s.name}» → L${layer + 1}`, true)}>
              <span>{s.name}</span><span className="muted">{s.instruments?.[0] ?? s.category ?? ''}</span>
            </button>
            <button type="button" className="icon-btn" aria-label={`Eliminar ${s.name}`} onClick={() => remove(s)}>×</button>
          </li>
        ))}
      </ul>
    </div>
  );
}

/** ● EDITADO for one piano layer Sound (settings, inserts, sends or Sound mappings not saved). */
function SoundDirty({ layer }: { layer: number }) {
  const dirty = useEngine(s => s?.soundDirty?.[layer] ?? false);
  return dirty ? <span className="dirty-tag" title="Cambios sin guardar en el sonido de esta capa">● EDITADO</span> : null;
}

/** Save the layer's Sound: overwrite the current one, or create a new one (optionally the sample's default). */
function SaveSoundDialog({ layer, current, onClose, onSaved }: {
  layer: number; current: SoundSummary | undefined; onClose: () => void; onSaved: () => void;
}) {
  const [name, setName] = useState(current?.name ?? '');
  const [asDefault, setAsDefault] = useState(!current);
  const overwrite = async () => {
    if (!current) return;
    if (await runAction(() => api.saveSound({ module: 'piano', layer, name: name.trim(), id: current.id }), `Sonido «${name.trim()}» actualizado`)) { onSaved(); onClose(); }
  };
  const saveNew = async () => {
    const r = await runAction(() => api.saveSound({ module: 'piano', layer, name: name.trim(), variant: true }), `Sonido «${name.trim()}» creado`);
    if (!r) return;
    if (asDefault) await runAction(() => api.setDefaultSound(r.sound.id), 'Predeterminado para este sample');
    onSaved(); onClose();
  };
  return (
    <Modal title={`GUARDAR SONIDO · L${layer + 1}`} onClose={onClose}>
      <form className="form" onSubmit={e => { e.preventDefault(); if (name.trim()) void (current ? overwrite() : saveNew()); }}>
        <Field label="Nombre"><input className="input" autoFocus maxLength={64} value={name} onChange={e => setName(e.target.value)} aria-label="Nombre del sonido" /></Field>
        <p className="hint">Guarda la configuración de la capa (amplitude, tone, voice/glide, mod wheel, sound EQ, inserts, envíos y mapeos de SONIDO).
          El sonido predeterminado se carga automáticamente cada vez que abras este sample.</p>
        <label className="mapping-menu__check"><input type="checkbox" checked={asDefault} onChange={e => setAsDefault(e.target.checked)} /> Predeterminado de este sample (al crear uno nuevo)</label>
        <Row>
          <span className="grow" />
          <Btn onClick={onClose}>CANCELAR</Btn>
          <Btn disabled={!name.trim()} onClick={() => void saveNew()}>GUARDAR COMO NUEVO</Btn>
          {current && <Btn variant="primary" type="submit" disabled={!name.trim()}>SOBRESCRIBIR «{current.name}»</Btn>}
        </Row>
      </form>
    </Modal>
  );
}

export function SampleEditor({ layer, onLayer }: { layer: number; onLayer: (l: number) => void }) {
  const [tab, setTab] = useState(0);
  const [saving, setSaving] = useState(false);
  const [sounds, setSounds] = useState<{ list: SoundSummary[]; selected: (string | null)[] }>({ list: [], selected: [] });
  const reloadSounds = useCallback(() => {
    api.sounds('piano').then(r => setSounds({
      list: r.sounds.filter(x => x.scope === 'layer'),
      selected: ((r.selected as { pianoLayers?: (string | null)[] }).pianoLayers ?? []),
    })).catch(() => {});
  }, []);
  // The layer's Sound changes when a sample, Sound or Performance is loaded.
  const rev = useEngine(s => s?.paramRev);
  useEffect(reloadSounds, [reloadSounds, layer, rev]);
  const current = sounds.list.find(x => x.id === sounds.selected[layer]);
  const hasSample = useEngine(s => Boolean(s?.pianoLayers?.[layer]?.sourcePath));
  const p = `pianoSample${layer}`;
  return (
    <Panel title="SAMPLE EDITOR" className="sample-editor" actions={<>
      <span className="sound-name" title="Sonido cargado en esta capa">{current ? `SONIDO: ${current.name}` : hasSample ? 'SIN SONIDO GUARDADO' : 'CAPA VACÍA'}</span>
      <SoundDirty layer={layer} />
      <Btn small variant="primary" disabled={!hasSample} onClick={() => setSaving(true)} title="Guardar la configuración de esta capa como sonido">GUARDAR SONIDO</Btn>
      <Segmented label="Capa" options={Array.from({ length: PIANO_LAYER_COUNT }, (_, i) => `L${i + 1}`)} value={layer} onChange={onLayer} />
    </>}>
      <div className="tabs" role="tablist">
        {TABS.map((t, i) => (
          <button key={t} type="button" role="tab" aria-selected={tab === i} className={`tabs__tab${tab === i ? ' is-active' : ''}`} onClick={() => setTab(i)}>{t}</button>
        ))}
      </div>
      <div className="tabs__panel" role="tabpanel">
        {tab === 0 && <div className="editor-grid editor-grid--panel"><EnvFilter p={p} /><Play p={p} /><Eq p={p} /></div>}
        {tab === 1 && <InsertChain unitPrefix={u => `pianoFx${layer}_${u}_`} />}
        {tab === 2 && <Sounds layer={layer} />}
      </div>
      {saving && <SaveSoundDialog layer={layer} current={current} onClose={() => setSaving(false)} onSaved={reloadSounds} />}
    </Panel>
  );
}
