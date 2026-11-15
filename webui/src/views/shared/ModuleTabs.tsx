// EDITOR · KEYZONES · FX RACK inside each module page: the module's own zones and
// effects rack, shown where the module is edited, to make its independence visible.
// The same components appear in the global ZONES and FX/MIX pages (overview for programs).
import { useState, type ReactNode } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { ModuleFx, RackView } from '../fx/FxView';
import { ModuleZones } from '../zones/ZonesView';
import { Segmented } from '../../ui/controls';

const TABS = ['editor', 'zones', 'fx'] as const;
const LABELS = ['EDITOR', 'KEYZONES', 'FX RACK'];
const NAMES = ['PIANO', 'SYNTH', 'ORGAN'];

function ModuleFxTab({ module }: { module: 0 | 1 | 2 }) {
  // Synth and organ also own a bus effect chain; the piano has the per-Sound inserts.
  const [part, setPart] = useState(0);
  return (
    <div className="module-fx-tab">
      {module !== 0 && <Segmented label="Efectos del módulo" options={['STUDIO RACK', `${NAMES[module]} BUS FX`]} value={part} onChange={setPart} />}
      {part === 0 || module === 0 ? <RackView rack={module} /> : <ModuleFx module={module === 1 ? 'synth' : 'organ'} />}
    </div>
  );
}

export function ModuleTabs({ module, children }: { module: 0 | 1 | 2; children: ReactNode }) {
  const [params, setParams] = useSearchParams();
  const tab = Math.max(0, TABS.indexOf((params.get('tab') ?? 'editor') as (typeof TABS)[number]));
  const select = (i: number) => setParams(i === 0 ? {} : { tab: TABS[i] }, { replace: true });
  return (
    <div className="view view--module">
      <nav className="subnav module-tabs" aria-label={`Secciones de ${NAMES[module]}`}>
        {LABELS.map((label, i) => (
          <button key={label} type="button" className={`subnav__item${tab === i ? ' is-active' : ''}`} aria-current={tab === i} onClick={() => select(i)}>{label}</button>
        ))}
        <span className="subnav__title">{NAMES[module]} · MÓDULO INDEPENDIENTE</span>
        {tab === 1 && <Link className="btn btn--sm" to="/zones">TODAS LAS ZONES ↗</Link>}
        {tab === 2 && <Link className="btn btn--sm" to={`/fx?rack=${module}`}>FX / MIX ↗</Link>}
      </nav>
      <div className="module-tabs__body">
        {tab === 0 && children}
        {tab === 1 && <div className="view view--zones"><ModuleZones module={module} /></div>}
        {tab === 2 && <div className="view view--fx-module"><ModuleFxTab module={module} /></div>}
      </div>
    </div>
  );
}
