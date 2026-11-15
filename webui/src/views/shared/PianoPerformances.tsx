// Piano Performances: a copy of the whole piano module (the five layers with their
// Sounds' settings, inserts and mappings, the mixer, key zones, rack and performance
// mappings). Performance values win over the Sounds they were built from.
import { useCallback, useEffect, useState, type ReactNode } from 'react';
import { api } from '../../api/client';
import type { SoundSummary } from '../../api/types';
import { useEngine } from '../../store/hooks';
import { runAction } from '../../store/notify';
import { Btn, Empty, Panel } from '../../ui/controls';
import { DirtyTag } from '../../ui/DirtyTag';

interface PerformanceEntry {
  name: string; layerCount?: number; legacy?: boolean;
  soundRefs?: (string | null)[];
  patch?: { pianoSources?: ({ name?: string; fileName?: string; sourcePath?: string } | null)[] };
}

/** What a performance plays: the Sound names, or the sample names for layers without a Sound. */
function layersOf(p: PerformanceEntry, sounds: Map<string, string>): string[] {
  const sources = p.patch?.pianoSources ?? [];
  const out: string[] = [];
  for (let i = 0; i < 5; i++) {
    const ref = p.soundRefs?.[i];
    const src = sources[i];
    if (!src?.sourcePath) continue;                         // empty layer
    const sample = src.name || src.fileName || src.sourcePath.split(/[\\/]/).pop() || '';
    if (ref && sounds.has(ref)) out.push(`L${i + 1} ${sounds.get(ref)}`);
    else if (sample) out.push(`L${i + 1} ${sample}`);
  }
  return out;
}

export function PianoPerformances({ className = '', children }: { className?: string; children?: ReactNode }) {
  const [items, setItems] = useState<PerformanceEntry[]>([]);
  const [sounds, setSounds] = useState<Map<string, string>>(new Map());
  const [name, setName] = useState('');
  const loadedLayers = useEngine(s => (s?.pianoLayers ?? []).filter(l => l?.sourcePath).length);
  const reload = useCallback(() => {
    api.pianoPerformances().then(r => setItems((r.performances ?? []) as PerformanceEntry[])).catch(() => {});
    api.sounds('piano').then(r => setSounds(new Map(r.sounds.map((s: SoundSummary) => [s.id, s.name])))).catch(() => {});
  }, []);
  useEffect(reload, [reload]);
  const act = async (fn: () => Promise<unknown>, msg: string, replace = false) => { if (await runAction(fn, msg, replace)) reload(); };
  const exists = items.some(p => p.name === name.trim() && !p.legacy);
  return (
    <Panel title="PIANO PERFORMANCES" className={className} actions={<DirtyTag module={0} />}>
      {children}
      <form className="row" onSubmit={e => { e.preventDefault(); if (name.trim()) void act(() => api.storePianoPerformance(name.trim()), 'Performance guardada').then(() => setName('')); }}>
        <input className="input grow" placeholder="Nombre de la Performance…" maxLength={64} value={name} onChange={e => setName(e.target.value)} aria-label="Nombre de la Performance" />
        <Btn variant="primary" type="submit" disabled={!name.trim()}>{exists ? 'SOBRESCRIBIR' : 'GUARDAR'}</Btn>
      </form>
      {loadedLayers < 2 && (
        <p className="hint">Recomendación: una Performance combina dos o más capas con sonido (layers o splits). Ahora hay {loadedLayers}.</p>
      )}
      <ul className="list list--scroll">
        {items.map(p => {
          const layers = layersOf(p, sounds);
          return (
            <li key={p.name} className="list__row">
              <button type="button" className="list__main" title={layers.join('\n')} onClick={() => act(() => api.loadPianoPerformance(p.name), `«${p.name}»`, true)}>
                <span>{p.name}</span>
                <span className="muted">{p.legacy ? 'legado' : layers.length ? layers.join(' · ') : `${p.layerCount ?? ''} capas`}</span>
              </button>
              {!p.legacy && <button type="button" className="icon-btn" aria-label={`Eliminar ${p.name}`} onClick={() => act(() => api.deletePianoPerformance(p.name), 'Eliminada')}>×</button>}
            </li>
          );
        })}
        {items.length === 0 && <Empty>Sin Performances.</Empty>}
      </ul>
    </Panel>
  );
}
