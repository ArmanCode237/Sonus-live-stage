// v32.4 network settings: OSC device (REAPER-style: this PC's IP / listen port, the remote
// controller's IP / port for feedback, sender filter) and HTTP access (browser / LAN).
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import { runAction } from '../../store/notify';
import { Btn, Field, Row } from '../../ui/controls';

type OscStatus = Awaited<ReturnType<typeof api.oscStatus>>;
type SystemStatus = Awaited<ReturnType<typeof api.system>>;

export function OscDevice() {
  const [st, setSt] = useState<OscStatus | null>(null);
  const [ip, setIp] = useState('');
  const [port, setPort] = useState('9000');
  useEffect(() => {
    const load = () => api.oscStatus().then(s => { setSt(s); return s; }).catch(() => null);
    void load().then(s => { if (s) { setIp(s.deviceIp); setPort(String(s.devicePort)); } });
    const t = setInterval(() => void load(), 1500);
    return () => clearInterval(t);
  }, []);
  const save = async (patch: Partial<{ deviceIp: string; devicePort: number; feedback: boolean; onlyFromDevice: boolean }>) => {
    const r = await runAction(() => api.setOsc(patch), 'OSC actualizado');
    if (r) setSt(r);
  };
  const local = st?.localAddresses ?? [];
  return (
    <div className="osc-device">
      <h3 className="subhead">OSC · CONEXIÓN POR IP</h3>
      <div className="osc-device__grid">
        <div className="osc-device__card">
          <span className="osc-device__label">ESTE EQUIPO (escribe esto en el controlador)</span>
          <span className="osc-device__value">{local.length ? local.join('  ·  ') : '—'}</span>
          <span className="osc-device__value">puerto de escucha <b>{st?.port ?? 9000}</b> {st?.running ? '· activo' : '· detenido'}</span>
        </div>
        <form className="osc-device__card" onSubmit={e => { e.preventDefault(); void save({ deviceIp: ip.trim(), devicePort: Number(port) || 9000 }); }}>
          <span className="osc-device__label">DISPOSITIVO (tablet, REAPER, TouchOSC…)</span>
          <Row>
            <Field label="IP del dispositivo"><input className="input" placeholder="192.168.1.20" value={ip} onChange={e => setIp(e.target.value)} /></Field>
            <Field label="Puerto"><input className="input osc-device__port" inputMode="numeric" value={port} onChange={e => setPort(e.target.value)} /></Field>
            <Btn type="submit" variant="primary">GUARDAR</Btn>
          </Row>
          <Row wrap>
            <Btn small led active={st?.feedback ?? false} disabled={!st?.deviceIp} onClick={() => void save({ feedback: !st?.feedback })}
              title="Envía al dispositivo el valor de cada parámetro mapeado por OSC cuando cambia (faders y botones siguen a la app)">FEEDBACK</Btn>
            <Btn small led active={st?.onlyFromDevice ?? false} disabled={!st?.deviceIp} onClick={() => void save({ onlyFromDevice: !st?.onlyFromDevice })}
              title="Ignora el OSC que llegue desde cualquier otra IP (recomendado en redes compartidas)">SOLO ESTA IP</Btn>
            <Btn small disabled={!st?.deviceIp} onClick={() => void runAction(() => api.oscTest(), 'Enviado /livestage/ping al dispositivo')}>PRUEBA</Btn>
          </Row>
        </form>
      </div>
      <p className="hint">Recibido: <b>{st?.received ?? 0}</b>{st?.lastSender ? <> · último de <b>{st.lastSender}</b>: <code>{st.lastAddress}</code></> : null}
        {st?.rejected ? <> · rechazados <b>{st.rejected}</b></> : null} · enviados <b>{st?.sent ?? 0}</b>.
        Acepta mensajes sueltos y <i>bundles</i> (float, int, double, true/false). Si no llega nada desde otro equipo, permite
        Live Stage en el Firewall de Windows (UDP {st?.port ?? 9000}).</p>
    </div>
  );
}

export function HttpAccess() {
  const [st, setSt] = useState<SystemStatus | null>(null);
  useEffect(() => { api.system().then(setSt).catch(() => setSt(null)); }, []);
  const save = async (patch: { webAccess?: boolean; lanAccess?: boolean }, msg: string) => {
    const r = await runAction(() => api.setSystem(patch), msg);
    if (r) setSt(r);
  };
  if (!st) return null;
  return (
    <div className="http-access">
      <Row wrap>
        <span className="live-safety__label">ACCESO WEB</span>
        <Btn small led active={st.webAccess || st.webAccessForced} disabled={st.webAccessForced}
          onClick={() => void save({ webAccess: !st.webAccess }, st.webAccess ? 'Acceso desde navegador desactivado' : 'Acceso desde navegador activado')}
          title="Permite abrir Live Stage en un navegador (localhost:8080). Desactivado, solo la ventana de la app puede usar el motor.">NAVEGADOR</Btn>
        <Btn small led active={st.lanAccess}
          onClick={() => void save({ lanAccess: !st.lanAccess }, st.lanAccess ? 'Red local desactivada (al reiniciar)' : 'Red local activada (al reiniciar)')}
          title="Escucha también en la red local (tablets, otro PC). Necesita NAVEGADOR activo y reiniciar la app.">RED LOCAL</Btn>
        <span className="hint">
          {st.webAccessForced ? 'Activo en esta sesión (modo navegador o --web). ' :
            st.webAccess ? 'Los navegadores de este equipo pueden controlar Live Stage. ' : 'Solo la ventana de Live Stage puede usar el motor. '}
          {st.restartRequired ? <b>Reinicia la app para aplicar RED LOCAL. </b> : null}
          {st.lanActive && st.lanUrls.length ? <>En la red: {st.lanUrls.map(u => <code key={u}>{u} </code>)}</> : null}
        </span>
      </Row>
    </div>
  );
}
