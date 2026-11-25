// v32.4 live-safety alerts: session recovery after an unexpected exit, samples missing
// from the 8 programs (checked at startup), PANIC and the MIDI CLOCK follow switch.
import { useState } from 'react';
import { api } from '../api/client';
import { useEngine } from '../store/hooks';
import { useMapAttrs } from '../store/mappings';
import { runAction } from '../store/notify';
import { Modal } from '../ui/Modal';
import { Btn, Row } from '../ui/controls';

/** Offered once after a crash / power loss: restore the autosaved edit buffer or discard it. */
export function SessionRecovery() {
  const info = useEngine(s => s?.sessionRecovery ?? null);
  const [dismissed, setDismissed] = useState(false);
  if (!info || dismissed) return null;
  const close = () => setDismissed(true);
  return (
    <Modal title="RECUPERAR SESIÓN" onClose={() => { void api.sessionDiscard(); close(); }}>
      <div className="form">
        <p>La sesión anterior terminó de forma inesperada. Hay un autoguardado de la edición en curso
          {info.programName ? <> («<b>{info.programName}</b>»)</> : null}{info.savedAt ? <> del <b>{info.savedAt}</b></> : null}.</p>
        <p className="hint">RESTAURAR recupera el buffer de edición (sonidos, capas, efectos y mapeos) sin tocar los programas guardados.</p>
        <Row>
          <span className="grow" />
          <Btn onClick={() => { void runAction(() => api.sessionDiscard(), 'Autoguardado descartado'); close(); }}>DESCARTAR</Btn>
          <Btn variant="primary" onClick={() => { void runAction(() => api.sessionRestore(), 'Sesión restaurada', true); close(); }}>RESTAURAR</Btn>
        </Row>
      </div>
    </Modal>
  );
}

const NO_MISSING: { slot: number; program: string; file: string }[] = [];

/** Header chip when a program references samples that are not on disk. */
export function MissingSamples() {
  const missing = useEngine(s => s?.missingSamples ?? NO_MISSING);   // stable fallback (no render loop)
  const [open, setOpen] = useState(false);
  if (!missing.length) return null;
  return (
    <>
      <button type="button" className="head-alert" onClick={() => setOpen(true)}
        title={`${missing.length} sample${missing.length > 1 ? 's' : ''} de los programas no se encuentra${missing.length > 1 ? 'n' : ''} en disco: clic para ver cuáles`}
        aria-label={`${missing.length} samples no encontrados`}>
        <span className="led is-on" aria-hidden="true" /><b className="head-alert__count">{missing.length}</b>
        <span className="head-alert__text">SAMPLE{missing.length > 1 ? 'S' : ''} <span className="head-alert__long">NO ENCONTRADO{missing.length > 1 ? 'S' : ''}</span><span className="head-alert__short">FALTA{missing.length > 1 ? 'N' : ''}</span></span>
      </button>
      {open && (
        <Modal title="SAMPLES NO ENCONTRADOS" onClose={() => setOpen(false)}>
          <p className="hint">Estos programas usan archivos que no se encontraron ni en su ruta guardada ni en las carpetas registradas en
            LIBRARY: esas capas sonarán vacías. Añade la carpeta que los contiene en LIBRARY (se buscan por nombre de archivo) o vuelve a
            cargar el sample antes de la presentación.</p>
          <ul className="list">{missing.map((m, i) => (
            <li key={i} className="list__row"><span className="list__main">PROGRAM {String(m.slot).padStart(2, '0')} · {m.program || '—'}</span>
              <code>{m.file}</code></li>))}</ul>
        </Modal>
      )}
    </>
  );
}

/** PANIC: every note off (with fades), sustain released, bend / wheel centred. Mappable (MAP). */
export function PanicButton() {
  const mapAttrs = useMapAttrs('panic');
  return (
    <button type="button" className="head-panic" {...mapAttrs} onClick={() => void runAction(() => api.panic(), 'PANIC: notas apagadas')}
      title="PANIC: apaga todas las notas (con fundido), suelta el pedal y centra bend y rueda. Mapeable a un pad o pedal en modo MAP.">
      PANIC
    </button>
  );
}

/** EXT: follow an external MIDI Clock (tempo + START restarts the synth sequencers). */
export function ClockSwitch() {
  const on = useEngine(s => s?.midiClockSync ?? false);
  const active = useEngine(s => s?.midiClockActive ?? false);
  const bpm = useEngine(s => s?.midiClockBpm ?? 0);
  return (
    <button type="button" className={`tempo__ext${on ? ' is-on' : ''}${active ? ' is-receiving' : ''}`} aria-pressed={on}
      onClick={() => void runAction(() => api.setParam('midiClockSync', on ? 0 : 1), on ? 'Tempo interno' : 'Tempo: MIDI CLOCK externo')}
      title={on ? `Sigue el MIDI Clock externo${active ? ` (${bpm.toFixed(1)} BPM)` : ' — sin señal de reloj'}` : 'EXT: seguir el MIDI Clock de un equipo externo'}>
      <span className="led" aria-hidden="true" />EXT
    </button>
  );
}
