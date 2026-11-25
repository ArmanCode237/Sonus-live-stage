// MT-32 layer (synthesis type 6): the 128 timbres of the emulated Roland MT-32
// (Munt / libmt32emu). Needs the user's own control + PCM ROM dumps.
import { useEffect, useMemo, useState } from 'react';
import { api } from '../../api/client';
import type { Mt32Status } from '../../api/types';
import { useEngine } from '../../store/hooks';
import { runAction } from '../../store/notify';
import { Empty } from '../../ui/controls';

let statusCache: Mt32Status | null = null;

export function Mt32Browser({ layer }: { layer: number }) {
  const [status, setStatus] = useState<Mt32Status | null>(statusCache);
  const [query, setQuery] = useState('');
  const program = useEngine(s => (s?.params?.synthLayers?.[layer] as { mt32Program?: number } | undefined)?.mt32Program ?? 0);
  useEffect(() => { if (!statusCache) api.mt32Status().then(s => { statusCache = s; setStatus(s); }).catch(() => {}); }, []);

  const sounds = useMemo(() => (status?.sounds ?? []).map((name, i) => ({ name, i }))
    .filter(s => !query.trim() || s.name.toLowerCase().includes(query.trim().toLowerCase())), [status, query]);
  const load = (i: number, name: string) => void runAction(() => api.mt32Load(layer, i), `MT-32 · ${name}`, true);

  return (
    <fieldset className="group group--wide dx7"><legend>MT-32 · TIMBRE</legend>
      <div className="dx7__lcd lcd">
        <div className="lcd__bar"><span>ROLAND MT-32 · LA32 (Munt)</span><span>{status?.loaded ? status.control : 'SIN ROMS'}</span></div>
        <div className="mini-lcd__name">
          {status?.loaded ? `${program < 64 ? 'A' : 'B'}${String((program % 64) + 1).padStart(2, '0')} ${status.sounds[program] ?? ''}` : '— — —'}
        </div>
      </div>
      {!status?.loaded ? (
        <div className="mt32__missing">
          <p className="hint">{status?.message ?? 'Comprobando ROMs…'}</p>
          {status?.folders?.length ? <ul className="mt32__folders">{status.folders.map(f => <li key={f}><code>{f}</code></li>)}</ul> : null}
          <p className="hint">Las ROMs del MT-32 son firmware de Roland y no se incluyen. Con tus propios volcados, esta capa suena como un
            MT-32 real (LA32, PCM, envolventes y reverb emulados por Munt). Sin ROMs, usa los presets LA del navegador de synth
            (parcial LA32 sintetizado).</p>
        </div>
      ) : (
        <>
          <input className="input" type="search" placeholder="Buscar timbre…" aria-label="Buscar timbre MT-32" value={query} onChange={e => setQuery(e.target.value)} />
          <ul className="dx7__voices" aria-label="Timbres MT-32">
            {sounds.map(s => (
              <li key={s.i}>
                <button type="button" className={`dx7__voice${s.i === program ? ' is-active' : ''}`} onClick={() => load(s.i, s.name)}>
                  <span className="dx7__num">{s.i < 64 ? 'A' : 'B'}{String((s.i % 64) + 1).padStart(2, '0')}</span>
                  <span className="dx7__name">{s.name}</span>
                </button>
              </li>
            ))}
            {sounds.length === 0 && <Empty>Sin resultados.</Empty>}
          </ul>
        </>
      )}
    </fieldset>
  );
}
