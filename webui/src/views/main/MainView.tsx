// PERFORM: the instrument front panel, laid out like a stage piano.
//   left    PIANO   OLED (sound no. + name), ◀ BROWSER ▶, five layers, quick edit
//   centre  big OLED program display, MASTER section, 8 program buttons (2 × 4)
//   right   SYNTH   OLED, category ◀ BROWSER ▶ (presets in a modal), layers, level, timbre
//           ORGAN   OLED, BROWSER (presets in a modal), drawbars, rotary, level, drive
// Deep editing lives on the PIANO / SYNTH / ORGAN pages.
import { useEffect, useMemo, useState } from 'react';
import { Link } from 'react-router-dom';
import { api } from '../../api/client';
import type { NamedPreset } from '../../api/types';
import { DRAWBAR_FOOTAGES, ORGAN_MODELS, SYNTH_LAYER_COUNT, SYNTH_LAYER_NAMES } from '../../data/constants';
import { SYNTH_FACTORY, SYNTH_FACTORY_CATEGORIES } from '../../data/synthFactory';
import { useEngine, useParam } from '../../store/hooks';
import { runAction } from '../../store/notify';
import { ALL_CATEGORY, FAVORITES_CATEGORY, displayName, filterFactory, isEdited, toggleFavorite, useFactoryMeta } from '../../store/synthFactory';
import { ParamKnob } from '../../ui/Knob';
import { PixelText } from '../../ui/PixelText';
import { useMapAttrs } from '../../store/mappings';
import { Btn, Empty, Lcd, Panel, ParamToggle, Select } from '../../ui/controls';
import { DirtyTag } from '../../ui/DirtyTag';
import { Modal } from '../../ui/Modal';
import { Drawbar } from '../shared/Drawbar';
import { MasterSection } from '../shared/Master';
import { PianoLayersModule } from '../shared/PianoLayers';
import { ProgramDisplay, ProgramPads } from '../shared/Programs';

const EditLink = ({ to }: { to: string }) => <Link className="btn btn--sm edit-link" to={to}>EDIT ↗</Link>;
const SYNTH_CATEGORIES = [ALL_CATEGORY, FAVORITES_CATEGORY, ...SYNTH_FACTORY_CATEGORIES];
const loadSynth = (id: number) => runAction(() => api.loadSynthFactory(id), `Synth ${String(id).padStart(3, '0')}`, true);

// ---------------------------------------------------------------- synth
/** Synth preset browser (modal): search, ALL / ★ FAVORITOS / categories, favourites. */
function SynthBrowser({ initialCategory, onClose }: { initialCategory: string; onClose: () => void }) {
  const presetId = Math.round(useEngine(s => s?.params?.synthPresetId ?? 1));
  const { favorites, overrides } = useFactoryMeta();
  const [category, setCategory] = useState(initialCategory);
  const [query, setQuery] = useState('');
  const list = useMemo(() => filterFactory(category, query, favorites, overrides), [category, query, favorites, overrides]);
  return (
    <Modal title="SYNTH · BROWSER" onClose={onClose} wide>
      <div className="browser">
        <div className="browser__bar">
          <input className="input browser__search" type="search" autoFocus placeholder="Buscar preset…" aria-label="Buscar preset de synth"
            value={query} onChange={e => setQuery(e.target.value)} />
          <Select label="Categoría de synth" value={category} options={SYNTH_CATEGORIES.map(c => ({ value: c, label: c }))}
            onChange={v => { setCategory(v); setQuery(''); }} />
          <span className="browser__count">{list.length} / {SYNTH_FACTORY.length}</span>
        </div>
        <ul className="browser__list">
          {list.map(p => (
            <li key={p.id} className="browser__row">
              <button type="button" className={`star${favorites.has(p.id) ? ' is-on' : ''}`} aria-pressed={favorites.has(p.id)}
                aria-label={`Favorito ${displayName(p, overrides)}`} onClick={() => void toggleFavorite(p.id)}>★</button>
              <button type="button" className={`browser__item${p.id === presetId ? ' is-active' : ''}`} onClick={() => void loadSynth(p.id)}>
                <span className="browser__num">{String(p.id).padStart(3, '0')}</span><span className="browser__name">{displayName(p, overrides)}</span>
                <span className="browser__meta">{p.category}</span>
              </button>
            </li>
          ))}
          {list.length === 0 && <Empty>{category === FAVORITES_CATEGORY ? 'Sin favoritos: pulsa ★.' : 'Sin resultados.'}</Empty>}
        </ul>
      </div>
    </Modal>
  );
}

function SynthModule() {
  const presetId = Math.round(useEngine(s => s?.params?.synthPresetId ?? 1));
  const layersOn = useEngine(s => (s?.params?.synthLayers ?? []).filter(l => l?.enabled).length);
  const current = SYNTH_FACTORY[presetId - 1];
  const { favorites, overrides } = useFactoryMeta();
  const [category, setCategory] = useState<string>('');
  const [browsing, setBrowsing] = useState(false);
  const cat = category || current?.category || SYNTH_FACTORY_CATEGORIES[0];
  const list = useMemo(() => filterFactory(cat, '', favorites, overrides), [cat, favorites, overrides]);
  // ◀ ▶ step through the chosen category (or favourites / the whole bank).
  const step = (d: number) => {
    const ids = list.length ? list.map(p => p.id) : SYNTH_FACTORY.map(p => p.id);
    const at = ids.indexOf(presetId);
    void loadSynth(ids[at < 0 ? (d > 0 ? 0 : ids.length - 1) : (at + d + ids.length) % ids.length]);
  };
  return (
    <Panel title="SYNTH" power={{ id: 'synthEnabled', label: 'Synth' }} className="panel--module mini-module synth-mini"
      actions={<><Btn small active={favorites.has(presetId)} onClick={() => void toggleFavorite(presetId)} title="Marcar el preset actual como favorito">★</Btn><EditLink to="/synth" /></>}>
      <Lcd className="mini-lcd">
        <div className="lcd__bar"><span>PATCH {String(presetId).padStart(3, '0')} / {SYNTH_FACTORY.length}</span>
          <span><DirtyTag module={1} /> {isEdited(presetId, overrides) ? '✎ USUARIO · ' : ''}{current?.category}</span></div>
        <div className="mini-lcd__name"><PixelText text={displayName(current, overrides)} /></div>
        <div className="lcd__foot"><span>CAPAS <b>{layersOn}/{SYNTH_LAYER_COUNT}</b></span><span>{favorites.has(presetId) ? '★ FAVORITO' : ''}</span></div>
      </Lcd>
      <div className="mini-browser__bar">
        <Btn small onClick={() => step(-1)} aria-label="Patch anterior">◀</Btn>
        <Select label="Categoría de synth" value={cat} options={SYNTH_CATEGORIES.map(c => ({ value: c, label: c }))} onChange={setCategory} />
        <Btn small variant="browse" onClick={() => setBrowsing(true)}>BROWSER</Btn>
        <Btn small onClick={() => step(1)} aria-label="Patch siguiente">▶</Btn>
      </div>
      <div className="mini-controls">
        <div className="mini-layers" role="group" aria-label="Capas del synth">
          {Array.from({ length: SYNTH_LAYER_COUNT }, (_, i) => <ParamToggle key={i} id={`L${i + 1}_en`} label={SYNTH_LAYER_NAMES[i]} small />)}
        </div>
        <ParamKnob id="synthMasterVol" label="LEVEL" size="sm" />
        <ParamKnob id="synthMacroCutoff" label="TIMBRE" size="sm" />
      </div>
      {browsing && <SynthBrowser initialCategory={cat} onClose={() => setBrowsing(false)} />}
    </Panel>
  );
}

// ---------------------------------------------------------------- organ
interface OrganEntry { key: string; name: string; kind: 'FACTORY' | 'USER'; load: () => Promise<unknown> }

function RotaryButton() {
  const { value, set } = useParam('organRotaryFast');
  const fast = (value ?? 0) >= 0.5;
  const mapAttrs = useMapAttrs('organRotaryFast');
  return (
    <div className="rotary-control">
      <span className="rotary-control__title">ROTARY</span>
      <Btn small active={fast} onClick={() => set(fast ? 0 : 1)} className="rotary-btn"
        aria-label="ROTARY · SLOW / FAST" aria-pressed={fast}
        title={`Rotary speaker: ${fast ? 'FAST' : 'SLOW'} (LED encendido = FAST). Clic derecho para mapear MIDI / OSC.`}
        {...mapAttrs}>
        {fast ? 'FAST' : 'SLOW'}
      </Btn>
    </div>
  );
}

function OrganModule() {
  const model = Math.round(useEngine(s => s?.params?.organModel ?? 0));
  const [factory, setFactory] = useState<{ factoryId: number; name: string }[]>([]);
  const [user, setUser] = useState<NamedPreset[]>([]);
  const [selected, setSelected] = useState('');
  const [browsing, setBrowsing] = useState(false);
  const [query, setQuery] = useState('');
  useEffect(() => {
    api.organFactory().then(setFactory).catch(() => {});
    api.organPresets().then(setUser).catch(() => {});
  }, []);
  const entries: OrganEntry[] = [
    ...factory.map(f => ({ key: `f${f.factoryId}`, name: f.name, kind: 'FACTORY' as const, load: () => api.loadOrganFactory(f.factoryId) })),
    ...user.map(u => ({ key: `u${u.name}`, name: u.name, kind: 'USER' as const, load: () => api.loadOrganPreset(u.name) })),
  ];
  const shown = entries.filter(e => !query.trim() || e.name.toLowerCase().includes(query.trim().toLowerCase()));
  const load = async (e: OrganEntry) => { if (await runAction(e.load, e.name, true)) setSelected(e.key); };
  const current = entries.find(e => e.key === selected);
  return (
    <Panel title="ORGAN" power={{ id: 'organEnabled', label: 'Organ' }} className="panel--module mini-module organ-mini"
      actions={<EditLink to="/organ" />}>
      <Lcd className="mini-lcd">
        <div className="lcd__bar"><span>{ORGAN_MODELS[model] ?? 'ORGAN'}</span><span><DirtyTag module={2} /> {current?.kind ?? ''}</span></div>
        <div className="mini-lcd__name"><PixelText text={current?.name ?? 'Registro actual'} /></div>
      </Lcd>
      <div className="mini-browser__bar">
        <Btn small variant="browse" onClick={() => setBrowsing(true)}>BROWSER</Btn>
        <RotaryButton />
        <ParamKnob id="organVolume" label="LEVEL" size="sm" />
        <ParamKnob id="organDrive" label="DRIVE" size="sm" />
      </div>
      <div className="mini-drawbars">{DRAWBAR_FOOTAGES.map((_, i) => <Drawbar key={i} index={i} />)}</div>
      {browsing && (
        <Modal title="ORGAN · BROWSER" onClose={() => setBrowsing(false)}>
          <div className="browser">
            <div className="browser__bar">
              <input className="input browser__search" type="search" autoFocus placeholder="Buscar preset…" aria-label="Buscar preset de órgano"
                value={query} onChange={e => setQuery(e.target.value)} />
              <span className="browser__count">{shown.length} / {entries.length}</span>
            </div>
            <ul className="browser__list browser__list--compact">
              {shown.map(e => (
                <li key={e.key}>
                  <button type="button" className={`browser__item${e.key === selected ? ' is-active' : ''}`} onClick={() => void load(e)}>
                    <span className="browser__num">{e.kind === 'FACTORY' ? 'F' : 'U'}</span><span className="browser__name">{e.name}</span>
                    <span className="browser__meta">{e.kind === 'FACTORY' ? 'FÁBRICA' : 'USUARIO'}</span>
                  </button>
                </li>
              ))}
              {shown.length === 0 && <Empty>Sin presets.</Empty>}
            </ul>
          </div>
        </Modal>
      )}
    </Panel>
  );
}

export function MainView() {
  const [layer, setLayer] = useState(0);
  return (
    <div className="view view--main">
      <section className="perform-col perform-col--piano" aria-label="Piano">
        <PianoLayersModule selected={layer} onSelect={setLayer} actions={<><DirtyTag module={0} /><EditLink to="/piano" /></>} quick />
      </section>
      <section className="perform-col perform-col--center" aria-label="Programa y master">
        <div className="performance-display"><ProgramDisplay /></div>
        <MasterSection />
        <ProgramPads />
      </section>
      <section className="perform-col perform-col--modules" aria-label="Synth y órgano">
        <SynthModule />
        <OrganModule />
      </section>
    </div>
  );
}
