// Sample library browser: pick an instrument file and load it into a piano layer.
// SF2 banks ask for the internal preset before loading.
import { useMemo, useState } from 'react';
import { api } from '../../api/client';
import type { LibraryItem } from '../../api/types';
import { libraryGroups, patchLibraryItem, useLibrary } from '../../store/library';
import { runAction } from '../../store/notify';
import { Btn, Empty, Segmented } from '../../ui/controls';
import { Modal } from '../../ui/Modal';

export function SampleBrowser({ layer, onClose }: { layer: number; onClose: () => void }) {
  const { items, error } = useLibrary();
  const [group, setGroup] = useState('ALL');
  const [query, setQuery] = useState('');
  const [favOnly, setFavOnly] = useState(false);
  const [busy, setBusy] = useState('');
  const [sf2, setSf2] = useState<{ item: LibraryItem; presets: string[] } | null>(null);

  const groups = useMemo(() => ['ALL', ...libraryGroups(items)], [items]);
  const visible = useMemo(() => {
    const q = query.trim().toLowerCase();
    return (items ?? []).filter(i =>
      (group === 'ALL' || i.group === group) && (!favOnly || i.favorite) && (!q || i.name.toLowerCase().includes(q)));
  }, [items, group, query, favOnly]);

  const load = async (item: LibraryItem, preset = -1) => {
    if (item.ext.toLowerCase() === '.sf2' && preset < 0) {
      setBusy(item.path);
      try {
        const r = await api.sf2Presets(item.path);
        if (r.presets.length > 1) { setSf2({ item, presets: r.presets }); return; }
        preset = 0;
      } catch { preset = 0; } finally { setBusy(''); }
    }
    setBusy(item.path);
    const result = await runAction(() => api.loadSample(item.path, layer, preset), `L${layer + 1}: ${item.name.replace(/\.[^.]+$/, '')}`, true);
    setBusy('');
    if (result) onClose();
  };
  const toggleFavorite = async (item: LibraryItem) => {
    const favorite = !item.favorite;
    patchLibraryItem(item.path, { favorite });
    await runAction(() => api.tagLibrary(item.name, { favorite }));
  };

  return (
    <Modal title={`CARGAR INSTRUMENTO EN CAPA L${layer + 1}`} onClose={onClose} wide>
      {sf2 ? (
        <div className="browser">
          <div className="browser__bar">
            <Btn small onClick={() => setSf2(null)}>← VOLVER</Btn>
            <span className="browser__title">{sf2.item.name} · {sf2.presets.length} presets</span>
          </div>
          <ul className="browser__list">
            {sf2.presets.map((name, i) => (
              <li key={i}><button type="button" className="browser__item" onClick={() => load(sf2.item, i)}>
                <span className="browser__num">{String(i).padStart(3, '0')}</span><span>{name}</span>
              </button></li>
            ))}
          </ul>
        </div>
      ) : (
        <div className="browser">
          <div className="browser__bar">
            <input className="input browser__search" type="search" placeholder="Buscar instrumento…" aria-label="Buscar instrumento"
              value={query} onChange={e => setQuery(e.target.value)} autoFocus />
            <Btn small led active={favOnly} onClick={() => setFavOnly(v => !v)}>FAVORITOS</Btn>
            <span className="browser__count">{visible.length} / {items?.length ?? 0}</span>
          </div>
          <Segmented options={groups} value={groups.indexOf(group)} onChange={i => setGroup(groups[i])} label="Grupo" wrap />
          {error && <Empty>{error}</Empty>}
          {!items && !error && <Empty>Leyendo la biblioteca…</Empty>}
          <ul className="browser__list">
            {visible.map(item => (
              <li key={item.path} className="browser__row">
                <button type="button" className={`star${item.favorite ? ' is-on' : ''}`} aria-pressed={item.favorite}
                  aria-label={`Favorito: ${item.name}`} onClick={() => toggleFavorite(item)}>★</button>
                <button type="button" className="browser__item" disabled={busy !== ''} onClick={() => load(item)}>
                  <span className="browser__name">{item.name.replace(/\.[^.]+$/, '')}</span>
                  <span className="browser__meta">{item.ext.slice(1).toUpperCase()} · {item.sizeMB.toFixed(0)} MB{item.ready ? '' : ' · sin caché'}</span>
                  {busy === item.path && <span className="browser__busy">CARGANDO…</span>}
                </button>
              </li>
            ))}
          </ul>
        </div>
      )}
    </Modal>
  );
}
