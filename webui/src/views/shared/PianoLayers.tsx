// Piano module shared by MAIN and PIANO: sound bank (category + ◀ ▶ + browser)
// and the five sample layers.
import { useMemo, useState } from 'react';
import { api } from '../../api/client';
import type { LibraryItem } from '../../api/types';
import { PIANO_LAYER_COUNT } from '../../data/constants';
import { useEngine } from '../../store/hooks';
import { libraryGroups, useLibrary } from '../../store/library';
import { runAction } from '../../store/notify';
import { ParamKnob } from '../../ui/Knob';
import { Btn, Lcd, Panel, ParamLed, Select } from '../../ui/controls';
import { PixelText } from '../../ui/PixelText';
import { SampleBrowser } from '../piano/SampleBrowser';

export const cleanName = (name: string | undefined) => (name ?? '').replace(/\s{2,}/g, ' ').trim();
const baseName = (file: string) => file.replace(/\.[^.]+$/, '');

export function LayerRow({ layer, selected, onSelect, onBrowse }: {
  layer: number; selected: boolean; onSelect: () => void; onBrowse: () => void;
}) {
  const name = useEngine(s => cleanName(s?.pianoLayers?.[layer]?.name));
  const hasSample = useEngine(s => Boolean(s?.pianoLayers?.[layer]?.sourcePath));
  const clear = () => runAction(() => api.clearPianoLayer(layer), `L${layer + 1} vaciada`, true);
  return (
    <div className={`layer-row${selected ? ' is-selected' : ''}`}>
      <ParamLed id={`pianoLayerEnabled${layer}`} label={`Capa L${layer + 1}`} />
      <button type="button" className="layer-row__tag" aria-pressed={selected} onClick={onSelect} title="Seleccionar capa">L{layer + 1}</button>
      <button type="button" className="layer-row__name" onClick={onBrowse} title="Cargar instrumento">
        {hasSample ? name || 'Instrumento' : <span className="muted">VACÍA · cargar…</span>}
      </button>
      {hasSample && <button type="button" className="icon-btn" aria-label={`Vaciar capa L${layer + 1}`} onClick={clear}>×</button>}
      <ParamKnob id={`pianoLayerVol${layer}`} label="LEVEL" size="sm" />
    </div>
  );
}

/** Category + step through instruments of that category into the selected layer. */
function SoundBank({ layer, onBrowse, pixelDisplay = false }: { layer: number; onBrowse: () => void; pixelDisplay?: boolean }) {
  const { items } = useLibrary();
  const fileName = useEngine(s => s?.pianoLayers?.[layer]?.fileName ?? '');
  const name = useEngine(s => cleanName(s?.pianoLayers?.[layer]?.name));
  const groups = useMemo(() => libraryGroups(items), [items]);
  const currentItem = useMemo(() => items?.find(i => i.name === fileName), [items, fileName]);
  const [chosen, setChosen] = useState<string | null>(null);
  const group = chosen ?? currentItem?.group ?? groups[0] ?? '';
  const inGroup = useMemo(() => (items ?? []).filter(i => i.group === group), [items, group]);
  const index = inGroup.findIndex(i => i.name === fileName);

  const load = (item: LibraryItem) =>
    runAction(() => api.loadSample(item.path, layer, item.ext.toLowerCase() === '.sf2' ? 0 : -1), `L${layer + 1}: ${baseName(item.name)}`, true);
  const step = (d: number) => {
    if (!inGroup.length) return;
    const next = index < 0 ? (d > 0 ? 0 : inGroup.length - 1) : (index + d + inGroup.length) % inGroup.length;
    void load(inGroup[next]);
  };
  return (
    <div className="sound-bank">
      <Select label="Categoría de instrumentos" value={group} className="sound-bank__group"
        options={groups.map(g => ({ value: g, label: g }))} onChange={setChosen} />
      <Lcd className="sound-bank__lcd">
        <div className="lcd__bar"><span>L{layer + 1}</span><span>{index >= 0 ? `${index + 1}/${inGroup.length}` : `—/${inGroup.length}`}</span></div>
        <div className="sound-bank__name">{pixelDisplay ? <PixelText text={name || 'VACÍA'} /> : name || 'VACÍA'}</div>
      </Lcd>
      <div className="sound-bank__nav">
        <Btn small onClick={() => step(-1)} aria-label="Instrumento anterior" disabled={!inGroup.length}>◀</Btn>
        <Btn small variant="browse" onClick={onBrowse}>BROWSER</Btn>
        <Btn small onClick={() => step(1)} aria-label="Instrumento siguiente" disabled={!inGroup.length}>▶</Btn>
      </div>
    </div>
  );
}

/** Quick access to the selected layer's most-played sample controls (MAIN). */
function LayerQuick({ layer }: { layer: number }) {
  const p = `pianoSample${layer}`;
  return (
    <div className="layer-quick">
      <span className="layer-quick__title">L{layer + 1} · QUICK EDIT</span>
      <ParamKnob id={`${p}Atk`} label="ATTACK" size="sm" />
      <ParamKnob id={`${p}Rel`} label="RELEASE" size="sm" />
      <ParamKnob id={`${p}Cut`} label="CUTOFF" size="sm" />
      <ParamKnob id={`${p}Gain`} label="GAIN" size="sm" />
      <ParamKnob id={`${p}Pan`} label="PAN" size="sm" />
    </div>
  );
}

export function PianoLayersModule({ selected, onSelect, actions, quick }: {
  selected: number; onSelect: (layer: number) => void; actions?: React.ReactNode;
  /** Show quick edit knobs for the selected layer (MAIN page). */
  quick?: boolean;
}) {
  const [browse, setBrowse] = useState<number | null>(null);
  return (
    <Panel title="PIANO" power={{ id: 'pianoEnabled', label: 'Piano' }} className="panel--module piano-module"
      actions={<>{actions}<ParamKnob id="pianoVolume" label="LEVEL" size="sm" /></>}>
      <SoundBank layer={selected} onBrowse={() => setBrowse(selected)} pixelDisplay={quick} />
      <div className="layers">
        {Array.from({ length: PIANO_LAYER_COUNT }, (_, l) => (
          <LayerRow key={l} layer={l} selected={selected === l} onSelect={() => onSelect(l)}
            onBrowse={() => { onSelect(l); setBrowse(l); }} />
        ))}
      </div>
      {quick && <LayerQuick layer={selected} />}
      {browse !== null && <SampleBrowser layer={browse} onClose={() => setBrowse(null)} />}
    </Panel>
  );
}
