// v32.4 sound browser, shared by piano, synth and organ (PERFORM and the module pages).
// Three columns: filters (origin, favourites, recents, categories, tags, engine) · results
// (search, sort, list / grouped / cards) · details (tags, category, actions).
// Live-safe: a click selects, double click / Enter / CARGAR loads; PRE-ESCUCHA (off by
// default) loads while moving with the arrow keys.
import { Fragment, useEffect, useMemo, useRef, useState, type KeyboardEvent, type ReactNode } from 'react';
import { Btn, Empty, Segmented } from '../ui/controls';
import { Modal } from '../ui/Modal';
import { recordUse, setCategory, setMetaFavorite, setTags, useBrowserMeta } from './meta';
import {
  applyFilters, EMPTY_FILTERS, facetCounts, groupRows, ORIGIN_LABEL, scopeOf, SORT_LABEL, sortItems,
  type BrowserModule, type Filters, type Origin, type ResolvedItem, type Scope, type SortKey, type SubItem,
} from './model';
import type { Provider } from './providers';

type View = 'list' | 'grouped' | 'cards';
interface Prefs { sort: SortKey; view: View; preview: boolean }
const PREFS_KEY = (m: BrowserModule) => `liveStage.browser.${m}`;
function readPrefs(m: BrowserModule): Prefs {
  try { return { sort: 'number', view: 'grouped', preview: false, ...(JSON.parse(localStorage.getItem(PREFS_KEY(m)) ?? '{}') as Partial<Prefs>) }; }
  catch { return { sort: 'number', view: 'grouped', preview: false }; }
}

const LAYER_LABELS: Record<BrowserModule, string[]> = { synth: ['A', 'B', 'C', 'D'], piano: ['L1', 'L2', 'L3', 'L4', 'L5'], organ: [] };

/** What a load replaces, per module: filter label, row badge and explanation. */
const SCOPE: Record<BrowserModule, Record<Scope, { facet: string; badge: string; title: string; text: string }>> = {
  synth: {
    module: { facet: 'PATCH COMPLETO', badge: 'PATCH A–D', title: 'PATCH COMPLETO',
      text: 'Reemplaza todo el sintetizador: las 4 capas (A–D), macros, LFO, modulación y efectos.' },
    layer: { facet: 'SOLO UNA CAPA', badge: 'CAPA', title: 'VOZ PARA UNA CAPA',
      text: 'Solo cambia la capa elegida. Las otras capas, las macros y los efectos no cambian.' },
  },
  piano: {
    module: { facet: 'PERFORMANCE', badge: 'L1–L5', title: 'PERFORMANCE COMPLETA',
      text: 'Reemplaza las 5 capas del piano con su configuración guardada.' },
    layer: { facet: 'SOLO UNA CAPA', badge: 'CAPA', title: 'INSTRUMENTO PARA UNA CAPA',
      text: 'Solo cambia la capa elegida. Las otras capas no cambian.' },
  },
  organ: {
    module: { facet: 'REGISTRO', badge: 'ÓRGANO', title: 'REGISTRO COMPLETO', text: 'Reemplaza el registro del órgano.' },
    layer: { facet: 'CAPA', badge: 'CAPA', title: 'CAPA', text: '' },
  },
};

export function SoundBrowser({ module, title, provider, layer = 0, onClose, extra }: {
  module: BrowserModule; title: string; provider: Provider; layer?: number; onClose: () => void;
  /** Source management shown under the filters (DX7 cartridges, MT-32 ROMs…). */
  extra?: ReactNode;
}) {
  const meta = useBrowserMeta();
  const [filters, setFilters] = useState<Filters>(EMPTY_FILTERS);
  const [prefs, setPrefsState] = useState<Prefs>(() => readPrefs(module));
  const [selected, setSelected] = useState<string | null>(provider.activeKey);
  const [target, setTarget] = useState(layer);
  const [sub, setSub] = useState<{ item: ResolvedItem; list: SubItem[] } | null>(null);
  const [busy, setBusy] = useState<string | null>(null);
  const searchRef = useRef<HTMLInputElement>(null);
  const listRef = useRef<HTMLDivElement>(null);
  const setPrefs = (p: Partial<Prefs>) => setPrefsState(prev => {
    const next = { ...prev, ...p };
    try { localStorage.setItem(PREFS_KEY(module), JSON.stringify(next)); } catch { /* private mode */ }
    return next;
  });
  const patch = (p: Partial<Filters>) => setFilters(f => ({ ...f, ...p }));

  // Items with the user metadata (tags, category, favourites, usage) applied.
  const resolved = useMemo<ResolvedItem[]>(() => provider.items.map(i => {
    const m = meta.items[i.key];
    const u = meta.usage[i.key];
    return {
      ...i,
      category: m?.category || i.baseCategory,
      tags: [...new Set([...(i.baseTags ?? []), ...(m?.tags ?? [])])],
      favorite: i.toggleSourceFavorite ? Boolean(i.sourceFavorite) : Boolean(m?.favorite),
      uses: u?.count ?? 0, lastUsed: u?.last ?? 0,
    };
  }), [provider.items, meta]);

  const results = useMemo(() => sortItems(applyFilters(resolved, filters), filters.recents ? 'recent' : prefs.sort), [resolved, filters, prefs.sort]);
  const rows = useMemo(() => groupRows(results, prefs.view === 'grouped' && !filters.recents), [results, prefs.view, filters.recents]);
  const facets = useMemo(() => ({
    origins: facetCounts(applyFilters(resolved, filters, 'origin'), i => i.origin) as [Origin, number][],
    scopes: facetCounts(applyFilters(resolved, filters, 'scope'), i => scopeOf(i)) as [Scope, number][],
    categories: facetCounts(applyFilters(resolved, filters, 'category'), i => i.category),
    tags: facetCounts(applyFilters(resolved, filters, 'tags'), i => i.tags, true).slice(0, 40),
    engines: facetCounts(applyFilters(resolved, filters, 'engine'), i => i.engine),
    favorites: applyFilters(resolved, filters, 'favorites').filter(i => i.favorite).length,
    recents: resolved.filter(i => i.lastUsed > 0).length,
  }), [resolved, filters]);
  const current = resolved.find(i => i.key === selected) ?? null;

  const load = async (item: ResolvedItem) => {
    if (item.subItems) {
      setBusy(item.key);
      const list = await item.subItems();
      setBusy(null);
      if (list) { setSub({ item, list }); return; }
    }
    setBusy(item.key);
    const done = await item.load({ layer: target });
    setBusy(null);
    if (done) recordUse(item.key);
  };
  const toggleFav = (item: ResolvedItem) => (item.toggleSourceFavorite ? void item.toggleSourceFavorite() : void setMetaFavorite(item.key, !item.favorite));

  // Keyboard: "/" search · ↑ ↓ move · Enter load · F favourite.
  const move = (delta: number) => {
    if (!results.length) return;
    const index = Math.max(0, results.findIndex(i => i.key === selected));
    const next = results[Math.min(results.length - 1, Math.max(0, (selected ? index + delta : 0)))];
    setSelected(next.key);
    listRef.current?.querySelector(`[data-key="${CSS.escape(next.key)}"]`)?.scrollIntoView({ block: 'nearest' });
  };
  const previewTimer = useRef<number>();
  useEffect(() => {
    if (!prefs.preview || !current || current.key === provider.activeKey || current.subItems) return;
    window.clearTimeout(previewTimer.current);
    previewTimer.current = window.setTimeout(() => void load(current), 280);
    return () => window.clearTimeout(previewTimer.current);
  }, [current?.key, prefs.preview]);   // eslint-disable-line react-hooks/exhaustive-deps
  const onKey = (e: KeyboardEvent) => {
    const typing = (e.target as HTMLElement).tagName === 'INPUT';
    if (e.key === 'ArrowDown') { e.preventDefault(); move(1); }
    else if (e.key === 'ArrowUp') { e.preventDefault(); move(-1); }
    else if (e.key === 'Enter' && current && !(typing && e.target !== searchRef.current)) { e.preventDefault(); void load(current); }
    else if (!typing && e.key === '/') { e.preventDefault(); searchRef.current?.focus(); }
    else if (!typing && (e.key === 'f' || e.key === 'F') && current) { e.preventDefault(); toggleFav(current); }
  };

  const layers = LAYER_LABELS[module];
  const scopeText = SCOPE[module];
  // The scope filter only makes sense when the module mixes both kinds of sounds.
  const mixedScopes = new Set(resolved.map(scopeOf)).size > 1;
  const anyFilter = filters.scope || filters.query || filters.origin !== 'all' || filters.favorites || filters.recents || filters.category || filters.tags.length || filters.engine;

  return (
    <Modal title={title} onClose={onClose} wide>
      <div className="sb" onKeyDown={onKey}>
        {/* ------------------------------------------------------------ filters */}
        <aside className="sb__filters" aria-label="Filtros">
          <div className="sb__group">
            <span className="sb__title">ORIGEN</span>
            <button type="button" className={`sb__facet${filters.origin === 'all' ? ' is-on' : ''}`} onClick={() => patch({ origin: 'all' })}>
              <span>TODOS</span><b>{facets.origins.reduce((n, [, c]) => n + c, 0)}</b></button>
            {facets.origins.map(([o, c]) => (
              <button key={o} type="button" className={`sb__facet sb__facet--${o}${filters.origin === o ? ' is-on' : ''}`} onClick={() => patch({ origin: filters.origin === o ? 'all' : o })}>
                <span>{ORIGIN_LABEL[o]}</span><b>{c}</b></button>))}
          </div>
          {mixedScopes && (
            <div className="sb__group">
              <span className="sb__title">QUÉ REEMPLAZA</span>
              {(['module', 'layer'] as Scope[]).map(sc => (
                <button key={sc} type="button" className={`sb__facet sb__facet--scope-${sc}${filters.scope === sc ? ' is-on' : ''}`}
                  title={scopeText[sc].text} onClick={() => patch({ scope: filters.scope === sc ? null : sc })}>
                  <span><i className={`sb__scope-dot sb__scope-dot--${sc}`} aria-hidden="true" />{scopeText[sc].facet}</span>
                  <b>{facets.scopes.find(([k]) => k === sc)?.[1] ?? 0}</b></button>))}
            </div>
          )}
          <div className="sb__group">
            <button type="button" className={`sb__facet${filters.favorites ? ' is-on' : ''}`} onClick={() => patch({ favorites: !filters.favorites })}>
              <span>★ FAVORITOS</span><b>{facets.favorites}</b></button>
            <button type="button" className={`sb__facet${filters.recents ? ' is-on' : ''}`} onClick={() => patch({ recents: !filters.recents })}>
              <span>⟲ RECIENTES</span><b>{Math.min(30, facets.recents)}</b></button>
          </div>
          <div className="sb__group sb__group--scroll">
            <span className="sb__title">CATEGORÍAS</span>
            {facets.categories.map(([c, n]) => (
              <button key={c} type="button" className={`sb__facet${filters.category === c ? ' is-on' : ''}`} onClick={() => patch({ category: filters.category === c ? null : c })}>
                <span>{c}</span><b>{n}</b></button>))}
          </div>
          {facets.engines.length > 1 && (
            <div className="sb__group">
              <span className="sb__title">{module === 'piano' ? 'FORMATO / TIPO' : 'MOTOR'}</span>
              {facets.engines.map(([e, n]) => (
                <button key={e} type="button" className={`sb__facet${filters.engine === e ? ' is-on' : ''}`} onClick={() => patch({ engine: filters.engine === e ? null : e })}>
                  <span>{e}</span><b>{n}</b></button>))}
            </div>
          )}
          {facets.tags.length > 0 && (
            <div className="sb__group">
              <span className="sb__title">ETIQUETAS</span>
              <div className="sb__chips">
                {facets.tags.map(([t, n]) => (
                  <button key={t} type="button" className={`sb__chip${filters.tags.includes(t) ? ' is-on' : ''}`} title={`${n} sonidos`}
                    onClick={() => patch({ tags: filters.tags.includes(t) ? filters.tags.filter(x => x !== t) : [...filters.tags, t] })}>#{t}</button>))}
              </div>
            </div>
          )}
          {extra && <div className="sb__group sb__extra"><p className="sb__title">FUENTES</p>{extra}</div>}
        </aside>

        {/* ------------------------------------------------------------ results */}
        <section className="sb__results" aria-label="Resultados">
          <div className="sb__bar">
            <input ref={searchRef} className="input sb__search" type="search" autoFocus placeholder="Buscar: palabras, categoría, #etiqueta…  ( / )"
              aria-label="Buscar sonido" value={filters.query} onChange={e => patch({ query: e.target.value })} />
            <select className="input sb__sort" aria-label="Ordenar por" value={prefs.sort} onChange={e => setPrefs({ sort: e.target.value as SortKey })}>
              {(Object.keys(SORT_LABEL) as SortKey[]).map(k => <option key={k} value={k}>ORDEN: {SORT_LABEL[k]}</option>)}
            </select>
            <div className="sb__views" role="group" aria-label="Vista">
              {([['list', '☰', 'Lista'], ['grouped', '▤', 'Por categoría'], ['cards', '▦', 'Tarjetas']] as [View, string, string][]).map(([v, icon, label]) => (
                <button key={v} type="button" className={`sb__view${prefs.view === v ? ' is-on' : ''}`} aria-pressed={prefs.view === v} title={label} onClick={() => setPrefs({ view: v })}>{icon}</button>))}
            </div>
          </div>
          <div className="sb__status">
            <span>{results.length} / {resolved.length}{provider.loading ? ' · cargando fuentes…' : ''}</span>
            {anyFilter ? <button type="button" className="sb__clear" onClick={() => setFilters(EMPTY_FILTERS)}>LIMPIAR FILTROS</button> : null}
            <label className="sb__preview" title="Carga el sonido al moverte con ↑ ↓ (útil en estudio; desactivado por seguridad en directo)">
              <input type="checkbox" checked={prefs.preview} onChange={e => setPrefs({ preview: e.target.checked })} /> PRE-ESCUCHA
            </label>
          </div>
          <div ref={listRef} className={`sb__list${prefs.view === 'cards' ? ' sb__list--cards' : ''}`} role="listbox" aria-label="Sonidos" tabIndex={-1}>
            {rows.map(r => r.kind === 'header'
              ? <div key={`h-${r.label}`} className="sb__header">{r.label}<span>{r.count}</span></div>
              : (
                <div key={r.item.key} data-key={r.item.key} role="option" aria-selected={r.item.key === selected}
                  className={`sb__row${r.item.key === selected ? ' is-selected' : ''}${r.item.key === provider.activeKey ? ' is-active' : ''}`}
                  onClick={() => setSelected(r.item.key)} onDoubleClick={() => void load(r.item)}>
                  <button type="button" className={`star${r.item.favorite ? ' is-on' : ''}`} aria-pressed={r.item.favorite} aria-label={`Favorito ${r.item.name}`}
                    onClick={e => { e.stopPropagation(); toggleFav(r.item); }}>★</button>
                  {r.item.number && <span className="sb__num">{r.item.number}</span>}
                  <span className="sb__name">{r.item.name}</span>
                  {prefs.view !== 'cards' && r.item.tags.slice(0, 3).map(t => <span key={t} className="sb__tag">#{t}</span>)}
                  <span className={`sb__origin sb__origin--${r.item.origin}`}>{ORIGIN_LABEL[r.item.origin]}</span>
                  {mixedScopes && (scopeOf(r.item) === 'layer'
                    ? <span className="sb__scope sb__scope--layer" title={scopeText.layer.text}>→ {scopeText.layer.badge} {layers[target]}</span>
                    : <span className="sb__scope sb__scope--module" title={scopeText.module.text}>{scopeText.module.badge}</span>)}
                  <span className="sb__detail">{busy === r.item.key ? 'CARGANDO…' : r.item.detail}</span>
                </div>
              ))}
            {!rows.length && <Empty>{provider.loading ? 'Cargando…' : anyFilter ? 'Sin resultados con estos filtros.' : 'No hay sonidos.'}</Empty>}
          </div>
        </section>

        {/* ------------------------------------------------------------ details */}
        <aside className="sb__details" aria-label="Detalles">
          {sub ? <SubList sub={sub} onBack={() => setSub(null)} onLoad={async s => { setBusy(sub.item.key); const ok = await s.load({ layer: target }); setBusy(null); if (ok) recordUse(sub.item.key); }} />
            : current ? <Details item={current} module={module} layers={layers} scope={scopeText[scopeOf(current)]} target={target} setTarget={setTarget}
              busy={busy === current.key} onLoad={() => void load(current)} onFavorite={() => toggleFav(current)}
              categories={facets.categories.map(([c]) => c)} />
              : <Empty>Selecciona un sonido para ver sus detalles. Doble clic o Enter lo carga.</Empty>}
        </aside>
      </div>
    </Modal>
  );
}

function SubList({ sub, onBack, onLoad }: { sub: { item: ResolvedItem; list: SubItem[] }; onBack: () => void; onLoad: (s: SubItem) => void }) {
  return (
    <div className="sb__sub">
      <Btn plain small onClick={onBack}>← VOLVER</Btn>
      <h3 className="sb__dname">{sub.item.name}</h3>
      <p className="hint">{sub.list.length} presets en este banco SF2: elige uno.</p>
      <ul className="sb__sublist">{sub.list.map(s => <li key={s.label}><button type="button" onClick={() => onLoad(s)}>{s.label}</button></li>)}</ul>
    </div>
  );
}

function Details({ item, module, layers, scope, target, setTarget, busy, onLoad, onFavorite, categories }: {
  item: ResolvedItem; module: BrowserModule; layers: string[]; scope: { title: string; text: string }; target: number; setTarget: (n: number) => void;
  busy: boolean; onLoad: () => void; onFavorite: () => void; categories: string[];
}) {
  const [tag, setTag] = useState('');
  const [mode, setMode] = useState<null | 'rename' | 'duplicate' | 'delete' | 'category'>(null);
  const [text, setText] = useState('');
  useEffect(() => { setMode(null); setTag(''); }, [item.key]);
  const userTags = item.tags.filter(t => !(item.baseTags ?? []).includes(t));
  const addTag = () => {
    const t = tag.trim().replace(/^#/, '').toLowerCase();
    if (t && !item.tags.includes(t)) void setTags(item.key, [...userTags, t]);
    setTag('');
  };
  const run = async (fn?: (v: string) => Promise<boolean>) => { if (fn && text.trim() && await fn(text.trim())) setMode(null); };
  return (
    <div className="sb__detail-card">
      <span className={`sb__origin sb__origin--${item.origin}`}>{ORIGIN_LABEL[item.origin]}{item.number ? ` · ${item.number}` : ''}</span>
      <h3 className="sb__dname">{item.name}</h3>
      <dl className="sb__info">
        <dt>Categoría</dt><dd>{item.category}{mode !== 'category' && <button type="button" className="sb__link" onClick={() => { setText(item.category); setMode('category'); }}>cambiar</button>}</dd>
        {item.engine && <><dt>{module === 'piano' ? 'Tipo' : 'Motor'}</dt><dd>{item.engine}</dd></>}
        {(item.info ?? []).map(([k, v]) => <Fragment key={k}><dt>{k}</dt><dd>{v}</dd></Fragment>)}
        <dt>Uso</dt><dd>{item.uses ? `${item.uses} ${item.uses === 1 ? 'vez' : 'veces'} · ${new Date(item.lastUsed).toLocaleDateString()}` : 'sin usar'}</dd>
      </dl>
      {mode === 'category' && (
        <form className="sb__inline" onSubmit={e => { e.preventDefault(); void setCategory(item.key, text.trim().toUpperCase() === item.baseCategory ? '' : text.trim()); setMode(null); }}>
          <input className="input" list="sb-categories" autoFocus value={text} onChange={e => setText(e.target.value)} aria-label="Categoría" />
          <datalist id="sb-categories">{categories.map(c => <option key={c} value={c} />)}</datalist>
          <Btn plain small type="submit">OK</Btn><Btn plain small onClick={() => { void setCategory(item.key, ''); setMode(null); }} title="Volver a la categoría original">ORIGINAL</Btn>
        </form>
      )}
      <div className="sb__tags">
        {item.tags.map(t => (
          <span key={t} className="sb__chip is-on">#{t}{userTags.includes(t) &&
            <button type="button" aria-label={`Quitar etiqueta ${t}`} onClick={() => void setTags(item.key, userTags.filter(x => x !== t))}>×</button>}</span>))}
        <form className="sb__tagadd" onSubmit={e => { e.preventDefault(); addTag(); }}>
          <input className="input" placeholder="+ etiqueta" value={tag} onChange={e => setTag(e.target.value)} aria-label="Nueva etiqueta" maxLength={32} />
        </form>
      </div>
      <div className={`sb__scopebox sb__scopebox--${scopeOf(item)}`} role="note">
        <strong><i className={`sb__scope-dot sb__scope-dot--${scopeOf(item)}`} aria-hidden="true" />{scope.title}</strong>
        <p>{scope.text}</p>
        {item.scopeNote && <p>{item.scopeNote}</p>}
        {item.needsLayer && layers.length > 0 && (
          <div className="sb__target"><span>CAPA DESTINO</span>
            <Segmented label="Capa destino" options={layers} value={target} onChange={setTarget} /></div>
        )}
      </div>
      <div className="sb__actions">
        <Btn plain variant="primary" className="sb__load" onClick={onLoad} disabled={busy}>
          {busy ? 'CARGANDO…' : item.subItems ? 'ELEGIR PRESET…' : item.needsLayer && layers.length ? `CARGAR EN CAPA ${layers[target]}` : 'CARGAR'}</Btn>
        <Btn plain small active={item.favorite} onClick={onFavorite}>{item.favorite ? '★ FAVORITO' : '☆ FAVORITO'}</Btn>
      </div>
      {(item.rename || item.duplicate || item.remove || item.restore) && (
        <div className="sb__manage">
          {item.rename && <Btn plain small onClick={() => { setText(item.name); setMode('rename'); }}>RENOMBRAR</Btn>}
          {item.duplicate && <Btn plain small onClick={() => { setText(`${item.name} (copia)`); setMode('duplicate'); }}>DUPLICAR</Btn>}
          {item.restore && <Btn plain small onClick={() => void item.restore?.()}>{item.restoreLabel ?? 'RESTAURAR'}</Btn>}
          {item.remove && <Btn plain small variant="danger" onClick={() => setMode('delete')}>BORRAR</Btn>}
        </div>
      )}
      {(mode === 'rename' || mode === 'duplicate') && (
        <form className="sb__inline" onSubmit={e => { e.preventDefault(); void run(mode === 'rename' ? item.rename : item.duplicate); }}>
          <input className="input" autoFocus maxLength={64} value={text} onChange={e => setText(e.target.value)} aria-label={mode === 'rename' ? 'Nuevo nombre' : 'Nombre de la copia'} />
          <Btn plain small variant="primary" type="submit" disabled={!text.trim()}>{mode === 'rename' ? 'RENOMBRAR' : 'DUPLICAR'}</Btn>
          <Btn plain small onClick={() => setMode(null)}>CANCELAR</Btn>
        </form>
      )}
      {mode === 'delete' && (
        <div className="sb__inline sb__confirm">
          <span>¿Borrar «{item.name}»? No se puede deshacer.</span>
          <Btn plain small variant="danger" onClick={() => void item.remove?.().then(ok => ok && setMode(null))}>SÍ, BORRAR</Btn>
          <Btn plain small onClick={() => setMode(null)}>NO</Btn>
        </div>
      )}
    </div>
  );
}
