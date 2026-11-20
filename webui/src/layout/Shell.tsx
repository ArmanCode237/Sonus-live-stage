// Sonus Live Stage Pro Console — unified navigation and control surface.
// The C++ engine remains the only source of state; this shell only presents it.
import { useEffect, useRef, useState } from 'react';
import { NavLink, Outlet, useLocation } from 'react-router-dom';
import { Keyboard } from '../ui/Keyboard';
import { Toasts } from '../ui/Toasts';
import { useConnection, useEngine, useParam, useTransport } from '../store/hooks';
import { useTheme } from '../store/theme';
import { useUiPrefs } from '../store/uiPrefs';
import { ClockSwitch, MissingSamples, PanicButton, SessionRecovery } from './LiveAlerts';
import { useMapAttrs } from '../store/mappings';
import { MappingLayer } from '../ui/MappingLayer';
import { BrandLogo } from '../ui/BrandLogo';
import { AboutModal } from './AboutModal';

// Instruments (independent modules) first, then the tools used to build programs.
const NAV = [
  { to: '/', label: 'PERFORM', icon: '◈', title: 'Performance', description: 'Todos tus instrumentos. Una sola superficie.' },
  { to: '/piano', label: 'PIANO', icon: '▥', title: 'Piano', description: 'Cinco capas, edición de muestras y performances.' },
  { to: '/synth', label: 'SYNTH', icon: '≋', title: 'Synthesizer', description: 'Cuatro capas de síntesis y control avanzado.' },
  { to: '/organ', label: 'ORGAN', icon: '▤', title: 'Organ', description: 'Drawbars, rotary y modelado de órgano.' },
  { to: '/fx', label: 'FX / MIX', icon: '◫', title: 'Effects & routing', description: 'Racks independientes, buses estéreo y envíos master.' },
  { to: '/zones', label: 'ZONES', icon: '⌗', title: 'Keyboard zones', description: 'Splits, rangos y asignaciones por capa.' },
  { to: '/library', label: 'LIBRARY', icon: '▦', title: 'Sound library', description: 'Muestras, sonidos y performances.' },
  { to: '/settings', label: 'SETTINGS', icon: '⚙', title: 'System settings', description: 'Audio, MIDI, OSC y apariencia.' },
] as const;

const KB_PREF = 'liveStage.keyboard';

function Status() {
  const connection = useConnection();
  const midi = useEngine(s => s?.connectedMidiCount ?? 0);
  const voices = useEngine(s => s?.activeVoices ?? 0);
  const maxVoices = useEngine(s => s?.maxVoices ?? 0);
  const pedal = useEngine(s => s?.pedalDown ?? false);
  const underflows = useEngine(s => s?.audioUnderflows ?? 0);
  const dsp = useEngine(s => s?.dspLoad ?? 0);
  const dspPeak = useEngine(s => s?.dspPeak ?? 0);
  const maxRender = useEngine(s => s?.maxRenderMicros ?? 0);
  const recovering = useEngine(s => s?.audioRecovering ?? false);
  const transport = useTransport();
  const { statusBar } = useUiPrefs();
  if (connection !== 'online') {
    return <div className="status status--offline" role="status"><span className="led" aria-hidden="true" />{connection === 'connecting' ? 'CONECTANDO…' : 'MOTOR DESCONECTADO'}</div>;
  }
  // v32.4 DSP load: render time / buffer time. Amber from 70 % average, red when a peak passes 90 %.
  const dspClass = dspPeak >= .9 ? ' is-bad' : dsp >= .7 ? ' is-warn' : '';
  const dspItem = (
    <span className={`status__item status__dsp${dspClass}`} title={`Carga DSP: media ${Math.round(dsp * 100)} %, pico ${Math.round(dspPeak * 100)} %, render máx. ${maxRender} µs. Por encima del 100 % hay cortes.`}>
      DSP <b>{Math.round(dsp * 100)}</b>%
    </span>
  );
  const alerts = (
    <>
      {recovering && <span className="status__item is-bad" title="La salida de audio se detuvo; el motor la está reabriendo">AUDIO REINTENTANDO…</span>}
      {underflows > 0 && <span className="status__item is-warn" title="Interrupciones de audio (se reinician en SETTINGS)">XRUN {underflows}</span>}
    </>
  );
  // SETTINGS → SYSTEM can hide the monitor; audio problems and DSP overload still show up.
  if (!statusBar) {
    const critical = recovering || underflows > 0 || dspPeak >= .9;
    return critical ? <div className="status" role="status">{alerts}{dspPeak >= .9 && dspItem}</div> : null;
  }
  return (
    <div className="status" role="status">
      <span className={`status__item${midi > 0 ? ' is-ok' : ' is-warn'}`}><span className={`led${midi > 0 ? ' is-on is-green' : ''}`} />MIDI {midi}</span>
      <span className="status__item">VOICES <b>{voices}</b>/{maxVoices}</span>
      <span className={`status__item${pedal ? ' is-ok' : ''}`}>PEDAL {pedal ? 'DOWN' : 'UP'}</span>
      {dspItem}
      <span className="status__item" title={transport === 'ws' ? 'WebSocket en tiempo real' : 'Comunicación HTTP'}>
        <span className={`led${transport === 'ws' ? ' is-on is-green' : ''}`} />{transport === 'ws' ? 'LIVE' : 'HTTP'}
      </span>
      {alerts}
    </div>
  );
}

function Tempo() {
  const { value, set } = useParam('masterBpm');
  const mapAttrs = useMapAttrs('masterBpm');
  const taps = useRef<number[]>([]);
  const [draft, setDraft] = useState<string | null>(null);
  const [flash, setFlash] = useState(0);
  const tap = () => {
    setFlash(f => f + 1);
    const now = performance.now();
    const list = taps.current.filter(t => now - t < 2500);
    list.push(now);
    taps.current = list.slice(-6);
    if (taps.current.length > 1) {
      const gaps = taps.current.slice(1).map((t, i) => t - taps.current[i]).sort((a, b) => a - b);
      set(Math.max(30, Math.min(300, Math.round(60000 / gaps[Math.floor(gaps.length / 2)]))));
    }
  };
  const commit = () => {
    if (draft !== null) { const n = Number(draft); if (draft.trim() && Number.isFinite(n)) set(Math.min(300, Math.max(30, n))); }
    setDraft(null);
  };
  return (
    <div className="tempo" {...mapAttrs}>
      <span className="tempo__label">TEMPO</span>
      <input className="tempo__input" inputMode="numeric" aria-label="Tempo en BPM" title="Escribe el tempo y pulsa Enter"
        value={draft ?? (value === undefined ? '' : String(Math.round(value)))}
        onChange={e => setDraft(e.target.value)} onBlur={commit}
        onKeyDown={e => { if (e.key === 'Enter') (e.target as HTMLInputElement).blur(); if (e.key === 'Escape') setDraft(null); }} />
      <span className="tempo__unit">BPM</span>
      <ClockSwitch />
      <button type="button" className="tempo__tap" onClick={tap} title="Pulsa al ritmo (2 o más toques) para fijar el tempo">
        <span key={flash} className={`led tempo__beat${flash ? ' is-flash' : ''}`} aria-hidden="true" />TAP
      </button>
    </div>
  );
}

const MAP_PREF = 'liveStage.mapMode';
/** MAP: outlines every mappable control and shows each mapping's source. */
function MapModeSwitch() {
  const [on, setOn] = useState<boolean>(() => { try { return localStorage.getItem(MAP_PREF) === '1'; } catch { return false; } });
  useEffect(() => {
    document.documentElement.classList.toggle('map-mode', on);
    try { localStorage.setItem(MAP_PREF, on ? '1' : '0'); } catch { /* private mode */ }
  }, [on]);
  return (
    <button type="button" className={`head-switch map-switch${on ? ' is-active' : ''}`} aria-pressed={on} onClick={() => setOn(v => !v)}
      title="Modo MAP: resalta los controles mapeables y muestra sus mapeos MIDI / OSC (clic derecho para editar)">
      <span className={`led${on ? ' is-on' : ''}`} aria-hidden="true" /><span className="head-btn__text">MAP</span>
    </button>
  );
}

function ThemeSwitch() {
  const [theme, setTheme] = useTheme();
  const next = theme === 'stage' ? 'studio' : 'stage';
  return (
    <button type="button" className="head-switch theme-switch" onClick={() => setTheme(next)}
      title={`Tema visual: ${theme === 'stage' ? 'Stage (rojo)' : 'Studio Dark'} · cambiar a ${next === 'stage' ? 'Stage' : 'Studio Dark'}`}
      aria-label={`Cambiar a tema ${next === 'stage' ? 'Stage' : 'Studio Dark'}`}>
      <span className={`theme-switch__swatch theme-switch__swatch--${theme}`} aria-hidden="true" />
      <span className="head-btn__text">{theme === 'stage' ? 'STAGE' : 'STUDIO'}</span>
    </button>
  );
}

export function Shell() {
  const { pathname } = useLocation();
  const connection = useConnection();
  const current = NAV.find(n => n.to === pathname) ?? NAV[0];
  const [keyboard, setKeyboard] = useState<boolean>(() => {
    try { const stored = localStorage.getItem(KB_PREF); return stored === null ? false : stored === '1'; }
    catch { return false; }
  });
  useEffect(() => { document.documentElement.classList.toggle('has-keyboard', keyboard); }, [keyboard]);
  useEffect(() => { document.title = `${current.title} · Sonus Live Stage`; }, [current.title]);
  const toggleKeyboard = () => setKeyboard(v => {
    try { localStorage.setItem(KB_PREF, v ? '0' : '1'); } catch { /* private mode */ }
    return !v;
  });
  const online = connection === 'online';
  const [about, setAbout] = useState(false);
  return (
    <div className="console">
      <header className="console__head">
        <button type="button" className="brand" onClick={() => setAbout(true)} aria-label="Sonus Live Stage — acerca de"
          title="Acerca de Sonus Live Stage">
          <BrandLogo size={34} className="brand__mark" />
          <span className="brand__copy">
            <strong>SONUS <span>LIVE STAGE</span></strong>
            <small className="crumb" title={online ? 'Motor conectado' : 'Motor desconectado: modo edición'}>
              <span className={`led${online ? ' is-on is-green' : ''}`} aria-hidden="true" />{current.label}
            </small>
          </span>
        </button>
        <h1 className="sr-only">{current.title}</h1>
        <nav className="nav" aria-label="Secciones principales">
          {[NAV.slice(0, 4), NAV.slice(4)].map((group, g) => (
            <div key={g} className={`nav__group nav__group--${g === 0 ? 'instruments' : 'tools'}`} role="group"
              aria-label={g === 0 ? 'Instrumentos' : 'Herramientas'}>
              {group.map(n => <NavLink key={n.to} to={n.to} end={n.to === '/'} title={`${n.title} — ${n.description}`} aria-label={n.label}
                className={({ isActive }) => `nav__link${isActive ? ' is-active' : ''}`}>
                <span className="nav__icon" aria-hidden="true">{n.icon}</span><span className="nav__text">{n.label}</span></NavLink>)}
            </div>
          ))}
        </nav>
        <MissingSamples />
        <Status />
        <Tempo />
        <PanicButton />
        <div className="head-group" role="group" aria-label="Vista">
          <ThemeSwitch />
          <MapModeSwitch />
          <button type="button" className={`head-switch kb-toggle${keyboard ? ' is-active' : ''}`} aria-pressed={keyboard}
            onClick={toggleKeyboard} aria-controls="stage-keyboard" aria-label="Teclado en pantalla" title="Mostrar u ocultar el teclado de 88 teclas">
            <span className={`led${keyboard ? ' is-on' : ''}`} aria-hidden="true" /><span className="head-btn__text">KEYS</span>
          </button>
        </div>
      </header>
      <main className="console__main" id="main-content"><Outlet /></main>
      {keyboard && <footer className="console__keys" id="stage-keyboard"><Keyboard /></footer>}
      <Toasts />
      <SessionRecovery />
      <MappingLayer />
      {about && <AboutModal onClose={() => setAbout(false)} />}
    </div>
  );
}
