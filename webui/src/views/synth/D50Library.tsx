// Roland D-50 patch banks (the user's own sysex dumps under roms/), translated on load
// to LA32 layers: synthesized partials with the LA32 cutoff arithmetic, PCM partials
// approximated by attack transients / LA32 sustain partials. Shown as a tab of the
// synth library overlay.
import { useEffect, useMemo, useState } from 'react';
import { api } from '../../api/client';
import type { D50Bank } from '../../api/types';
import { runAction } from '../../store/notify';
import { Empty, Select } from '../../ui/controls';

let bankCache: D50Bank[] | null = null;

export function D50List() {
  const [banks, setBanks] = useState<D50Bank[] | null>(bankCache);
  const [bankId, setBankId] = useState('');
  const [query, setQuery] = useState('');
  useEffect(() => { if (!bankCache) api.d50Banks().then(r => { bankCache = r.banks; setBanks(r.banks); }).catch(() => setBanks([])); }, []);
  useEffect(() => { if (!bankId && banks?.length) setBankId(banks[0].id); }, [banks, bankId]);
  const bank = banks?.find(b => b.id === bankId);
  const list = useMemo(() => (bank?.patches ?? []).filter(p => {
    const q = query.trim().toLowerCase();
    return !q || `${p.name} ${p.upper} ${p.lower}`.toLowerCase().includes(q);
  }), [bank, query]);
  if (banks === null) return <p className="hint">Buscando bancos…</p>;
  if (banks.length === 0) return <p className="hint">Copia volcados .syx del D-50 en la carpeta <code>roms</code> del proyecto (por ejemplo roms/PN-D50-00).</p>;
  return (
    <div className="browser">
      <div className="browser__bar">
        <input className="input browser__search" type="search" placeholder="Buscar patch o tono…" aria-label="Buscar patch D-50"
          value={query} onChange={e => setQuery(e.target.value)} />
        <Select label="Banco D-50" value={bankId} options={banks.map(b => ({ value: b.id, label: b.name }))} onChange={setBankId} />
        <span className="browser__count">{list.length} / {bank?.patches.length ?? 0}</span>
      </div>
      <p className="hint">Se traducen a capas LA32: los parciales sintéticos son fieles; los PCM se aproximan (la ROM de ondas del D-50 no está disponible).</p>
      <ul className="browser__list">
        {list.map(p => (
          <li key={p.n}>
            <button type="button" className="browser__item" onClick={() => void runAction(() => api.d50Load(bank!.id, p.n), `D-50 · ${p.name}`, true)}>
              <span className="browser__num">{Math.floor((p.n - 1) / 8) + 1}{((p.n - 1) % 8) + 1}</span>
              <span className="browser__name">{p.name}</span>
              <span className="browser__meta">{p.mode} · {p.upper}{p.mode !== 'WHOLE' ? ` / ${p.lower}` : ''}</span>
            </button>
          </li>
        ))}
        {list.length === 0 && <Empty>Sin resultados.</Empty>}
      </ul>
    </div>
  );
}
