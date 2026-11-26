// PERFORM: the instrument front panel, laid out like a stage piano.
//   left    PIANO   OLED (sound no. + name), ◀ BROWSER ▶, five layers, quick edit
//   centre  big OLED program display, MASTER section, 8 program buttons (2 × 4)
//   right   SYNTH   OLED, category ◀ BROWSER ▶ (presets in a modal), layers, level, timbre
//           ORGAN   OLED, BROWSER (presets in a modal), drawbars, rotary, level, drive
// Deep editing lives on the PIANO / SYNTH / ORGAN pages.
import { useMemo, useState } from 'react';
import { Link } from 'react-router-dom';
import { api } from '../../api/client';
import { DRAWBAR_FOOTAGES, ORGAN_MODELS, SYNTH_LAYER_COUNT, SYNTH_LAYER_NAMES } from '../../data/constants';
import { SYNTH_FACTORY, SYNTH_FACTORY_CATEGORIES } from '../../data/synthFactory';
import { useEngine, useParam } from '../../store/hooks';
import { runAction } from '../../store/notify';
import { ALL_CATEGORY, FAVORITES_CATEGORY, displayName, filterFactory, isEdited, toggleFavorite, useFactoryMeta } from '../../store/synthFactory';
import { ParamKnob } from '../../ui/Knob';
import { PixelText } from '../../ui/PixelText';
import { useMapAttrs } from '../../store/mappings';
import { Btn, Lcd, Panel, ParamToggle, Select } from '../../ui/controls';
import { DirtyTag } from '../../ui/DirtyTag';
import { OrganSoundBrowser, SynthSoundBrowser } from '../../browser/ModuleBrowsers';
import { useOrganPresetRef, useSynthPresetRef } from '../../store/synthUserPatch';
import { Drawbar } from '../shared/Drawbar';
import { MasterSection } from '../shared/Master';
import { PianoLayersModule } from '../shared/PianoLayers';
import { ProgramDisplay, ProgramPads } from '../shared/Programs';

const EditLink = ({ to }: { to: string }) => <Link className="btn btn--sm edit-link" to={to}>EDIT ↗</Link>;
const SYNTH_CATEGORIES = [ALL_CATEGORY, FAVORITES_CATEGORY, ...SYNTH_FACTORY_CATEGORIES];
const loadSynth = (id: number) => runAction(() => api.loadSynthFactory(id), `Synth ${String(id).padStart(3, '0')}`, true);

// ---------------------------------------------------------------- synth
function SynthModule() {
  const presetId = Math.round(useEngine(s => s?.params?.synthPresetId ?? 1));
  const layersOn = useEngine(s => (s?.params?.synthLayers ?? []).filter(l => l?.enabled).length);
  const current = SYNTH_FACTORY[presetId - 1];
  const shownUser = useSynthPresetRef();
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
      actions={<><Btn small active={!shownUser && favorites.has(presetId)} disabled={Boolean(shownUser)}
        onClick={() => void toggleFavorite(presetId)}
        title={shownUser ? 'Favoritos: solo para presets de fábrica (usa el BROWSER para el resto)' : 'Marcar el preset actual como favorito'}>★</Btn><EditLink to="/synth" /></>}>
      <Lcd className="mini-lcd">
        {/* A user / D-50 patch is not a factory number: show its origin instead of a stale "PATCH 001". */}
        <div className="lcd__bar"><span>{shownUser ? (shownUser.kind === 'USER' ? 'USUARIO' : shownUser.kind) : `PATCH ${String(presetId).padStart(3, '0')} / ${SYNTH_FACTORY.length}`}</span>
          <span><DirtyTag module={1} /> {shownUser ? '' : `${isEdited(presetId, overrides) ? '✎ EDITADO · ' : ''}${current?.category ?? ''}`}</span></div>
        <div className="mini-lcd__name"><PixelText text={shownUser?.name ?? displayName(current, overrides)} /></div>
        <div className="lcd__foot"><span>CAPAS <b>{layersOn}/{SYNTH_LAYER_COUNT}</b></span><span>{!shownUser && favorites.has(presetId) ? '★ FAVORITO' : ''}</span></div>
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
      {browsing && <SynthSoundBrowser onClose={() => setBrowsing(false)} />}
    </Panel>
  );
}

// ---------------------------------------------------------------- organ

function RotaryButton() {
  const { value, set } = useParam('organRotaryFast');
  const fast = (value ?? 0) >= 0.5;
  const mapAttrs = useMapAttrs('organRotaryFast');
  return (
    // Same anatomy as the knobs beside it: control · label · value box (rows line up).
    <div className="rotary-control">
      <Btn small active={fast} onClick={() => set(fast ? 0 : 1)} className="rotary-btn"
        aria-label="ROTARY · SLOW / FAST" aria-pressed={fast}
        title={`Rotary speaker: ${fast ? 'FAST' : 'SLOW'} (LED encendido = FAST). Clic derecho para mapear MIDI / OSC.`}
        {...mapAttrs}>
        ROTARY
      </Btn>
      <output className={`knob__value rotary-control__value${fast ? ' is-fast' : ''}`}>{fast ? 'FAST' : 'SLOW'}</output>
    </div>
  );
}

function OrganModule() {
  const model = Math.round(useEngine(s => s?.params?.organModel ?? 0));
  const [browsing, setBrowsing] = useState(false);
  const current = useOrganPresetRef();
  return (
    <Panel title="ORGAN" power={{ id: 'organEnabled', label: 'Organ' }} className="panel--module mini-module organ-mini"
      actions={<EditLink to="/organ" />}>
      <Lcd className="mini-lcd">
        <div className="lcd__bar"><span>{ORGAN_MODELS[model] ?? 'ORGAN'}</span><span><DirtyTag module={2} /> {current?.kind ?? ''}</span></div>
        <div className="mini-lcd__name"><PixelText text={current?.name ?? 'Registro actual'} /></div>
      </Lcd>
      <div className="mini-browser__bar">
        {/* Empty value row: BROWSER keeps the control · label · value rows of its neighbours. */}
        <div className="rotary-control">
          <Btn small variant="browse" onClick={() => setBrowsing(true)}>BROWSER</Btn>
          <output className="knob__value rotary-control__spacer" aria-hidden="true">&nbsp;</output>
        </div>
        <RotaryButton />
        <ParamKnob id="organVolume" label="LEVEL" size="sm" />
        <ParamKnob id="organDrive" label="DRIVE" size="sm" />
      </div>
      <div className="mini-drawbars">{DRAWBAR_FOOTAGES.map((_, i) => <Drawbar key={i} index={i} />)}</div>
      {browsing && <OrganSoundBrowser onClose={() => setBrowsing(false)} />}
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
