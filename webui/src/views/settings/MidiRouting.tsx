// v33 MIDI ROUTING: which keyboard / channel plays which piano layers, synth layers and the
// organ (Reaper style). Off = everything plays everything. Changes apply immediately; notes
// already sounding finish with the routing they started with.
import { useEffect, useState } from 'react';
import { api, type MidiRouteRule, type MidiRouting as Routing } from '../../api/client';
import { runAction } from '../../store/notify';
import { Btn, Empty, Panel } from '../../ui/controls';
import {
  ALL_MASK, coverage, DEST_LABELS, describeDest, hasBit, ORGAN_MASK, PIANO_MASK, SYNTH_MASK,
  TEMPLATE_ALL, TEMPLATE_CHANNELS, templateKeyboards, toggleBit, toggleGroup,
} from './midiRoutingModel';

export function MidiRouting() {
  const [routing, setRouting] = useState<Routing | null>(null);
  useEffect(() => { api.midiRouting().then(setRouting).catch(() => setRouting(null)); }, []);
  if (!routing) return <Panel title="MIDI ROUTING"><Empty>Leyendo el enrutado MIDI…</Empty></Panel>;

  const save = async (patch: { enabled?: boolean; rules?: MidiRouteRule[]; pedalFollows?: boolean }, label?: string) => {
    setRouting({ ...routing, ...patch });                                   // optimistic
    const r = await runAction(() => api.setMidiRouting(patch), label);
    if (r) setRouting(r);
  };
  const setRule = (i: number, rule: MidiRouteRule) => void save({ rules: routing.rules.map((x, k) => (k === i ? rule : x)) });
  const ports = routing.sources.filter(s => s.id !== 0 && s.enabled).map(s => s.name);
  const sourceLabel = (name: string) =>
    name === '*' ? 'CUALQUIERA' : routing.sources.find(s => s.name === name)?.label ?? name;
  const { silentSources, unreachedMask } = coverage(routing.rules, ['internal', ...ports]);
  const plugin = routing.host === 'plugin';

  return (
    <Panel title="MIDI ROUTING" className="cfg-routing"
      actions={<Btn small led active={routing.enabled} onClick={() => void save({ enabled: !routing.enabled },
        routing.enabled ? 'Enrutado MIDI desactivado: todo toca todo' : 'Enrutado MIDI activado')}>{routing.enabled ? 'ACTIVADO' : 'DESACTIVADO'}</Btn>}>
      <p className="hint">{plugin
        ? 'En el plug-in, cada canal MIDI de la pista puede tocar capas distintas (multitímbrico): p. ej. canal 1 → piano, canal 2 → synth.'
        : 'Asigna cada teclado (y/o canal) a capas de piano, capas del synth o al órgano. Desactivado, todo el MIDI toca todo, como siempre.'}
        {' '}Las notas que ya suenan terminan con el enrutado con el que empezaron.</p>
      <div className="routing-templates">
        <span className="live-safety__label">PLANTILLAS</span>
        <button type="button" className="routing-chip routing-chip--group routing-chip--text" onClick={() => void save({ enabled: true, rules: TEMPLATE_ALL }, 'Todo a todo')}>TODO A TODO</button>
        <button type="button" className="routing-chip routing-chip--group routing-chip--text" onClick={() => void save({ enabled: true, rules: TEMPLATE_CHANNELS }, 'Canal 1 piano · 2 synth · 3 órgano')}>CANAL 1·2·3 → PIANO·SYNTH·ORGAN</button>
        {!plugin && ports.length >= 2 && (
          <button type="button" className="routing-chip routing-chip--group routing-chip--text" onClick={() => void save({ enabled: true, rules: templateKeyboards(ports) }, 'Teclado 1 piano · teclado 2 synth')}>
            {ports.length >= 3 ? 'TECLADOS 1·2·3 → PIANO·SYNTH·ORGAN' : 'TECLADO 1 → PIANO · TECLADO 2 → SYNTH'}</button>
        )}
      </div>
      <div className={`routing-rules${routing.enabled ? '' : ' is-off'}`} role="table" aria-label="Reglas de enrutado MIDI">
        <div className="routing-rule routing-rule--head" role="row">
          <span>ENTRADA</span><span>CANAL</span><span>DESTINOS</span><span />
        </div>
        {routing.rules.map((rule, i) => (
          <div key={i} className="routing-rule" role="row">
            <select className="input" aria-label={`Entrada de la regla ${i + 1}`} value={rule.source}
              onChange={e => setRule(i, { ...rule, source: e.target.value })}>
              <option value="*">CUALQUIERA</option>
              {routing.sources.map(s => <option key={s.name} value={s.name}>{s.label}{s.connected ? '' : ' (desconectado)'}</option>)}
              {rule.source !== '*' && !routing.sources.some(s => s.name === rule.source) && <option value={rule.source}>{rule.source} (no conectado)</option>}
            </select>
            <select className="input" aria-label={`Canal de la regla ${i + 1}`} value={rule.channel}
              onChange={e => setRule(i, { ...rule, channel: Number(e.target.value) })}>
              <option value={0}>OMNI</option>
              {Array.from({ length: 16 }, (_, c) => <option key={c} value={c + 1}>CANAL {c + 1}</option>)}
            </select>
            <div className="routing-dest" role="group" aria-label={`Destinos de la regla ${i + 1}: ${describeDest(rule.dest)}`}>
              {([['PIANO', PIANO_MASK, 0, 5], ['SYNTH', SYNTH_MASK, 5, 4], ['ORGAN', ORGAN_MASK, 9, 1]] as const).map(([name, group, first, count]) => (
                <div key={name} className="routing-dest__group">
                  <button type="button" className={`routing-chip routing-chip--group${(rule.dest & group) === group ? ' is-on' : ''}`}
                    aria-pressed={(rule.dest & group) === group} onClick={() => setRule(i, { ...rule, dest: toggleGroup(rule.dest, group) })}>{name}</button>
                  {count > 1 && Array.from({ length: count }, (_, k) => first + k).map(bit => (
                    <button key={bit} type="button" className={`routing-chip${hasBit(rule.dest, bit) ? ' is-on' : ''}`} aria-pressed={hasBit(rule.dest, bit)}
                      aria-label={`${name} ${DEST_LABELS[bit]}`} onClick={() => setRule(i, { ...rule, dest: toggleBit(rule.dest, bit) })}>{DEST_LABELS[bit]}</button>
                  ))}
                </div>
              ))}
            </div>
            <button type="button" className="icon-btn" aria-label={`Quitar la regla ${i + 1}`}
              onClick={() => void save({ rules: routing.rules.filter((_, k) => k !== i) })}>×</button>
          </div>
        ))}
        {routing.rules.length === 0 && <Empty>Sin reglas: con el enrutado activado, el MIDI no toca nada.</Empty>}
      </div>
      <div className="routing-footer">
        <button type="button" className={`routing-chip routing-chip--group routing-chip--text${routing.pedalFollows !== false ? ' is-on' : ''}`}
          aria-pressed={routing.pedalFollows !== false}
          title="Encendido: el pedal de sustain de cada teclado / canal sostiene solo las capas que toca. Apagado: cualquier pedal sostiene todo."
          onClick={() => void save({ pedalFollows: routing.pedalFollows === false }, routing.pedalFollows === false ? 'Pedal por teclado' : 'Un pedal sostiene todo')}>
          PEDAL POR TECLADO</button>
        <button type="button" className="routing-chip routing-chip--group routing-chip--text"
          onClick={() => void save({ rules: [...routing.rules, { source: ports[0] ?? '*', channel: 0, dest: ALL_MASK }] })}>+ REGLA</button>
        {routing.enabled && silentSources.length > 0 && (
          <span className="routing-warn">Sin regla (no suenan): {silentSources.map(sourceLabel).join(', ')}</span>)}
        {routing.enabled && unreachedMask !== 0 && unreachedMask !== ALL_MASK && (
          <span className="routing-warn">Ninguna regla toca: {describeDest(unreachedMask)}</span>)}
      </div>
    </Panel>
  );
}
