// OLED displays of the synth editor (visual only): oscillator waveform, filter
// response and LFO shape. They read the layer parameters and redraw as knobs move.
import { useMemo } from 'react';
import { SYNTH_FILTER_TYPES, SYNTH_LFO_WAVES, SYNTH_SYNTHESIS_TYPES, SYNTH_WAVES } from '../../data/constants';
import { useParam } from '../../store/hooks';

const W = 200, H = 64;
const pv = (id: string, fallback: number) => useParam(id).value ?? fallback;   // eslint-disable-line react-hooks/rules-of-hooks

function Screen({ title, info, children, className = '' }: { title: string; info?: string; children: React.ReactNode; className?: string }) {
  return (
    <div className={`synth-screen ${className}`}>
      <div className="synth-screen__bar"><span>{title}</span>{info && <span>{info}</span>}</div>
      <svg className="synth-screen__svg" viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none" aria-hidden="true">
        <line className="synth-screen__axis" x1={0} x2={W} y1={H / 2} y2={H / 2} />
        {children}
      </svg>
    </div>
  );
}

// ---------------------------------------------------------------- oscillator
const frac = (x: number) => x - Math.floor(x);
function waveSample(type: number, p: number, pw: number): number {
  switch (type) {
    case 1: case 11: case 21: case 35: return frac(p) < pw ? 1 : -1;               // pulse family
    case 2: case 28: return 1 - 4 * Math.abs(frac(p) - .5);                        // triangle
    case 3: case 15: return Math.sin(2 * Math.PI * p);                             // sine
    case 4: case 33: return ((2 * frac(p) - 1) + (2 * frac(p * 1.01 + .3) - 1) * .7 + (2 * frac(p * .99 + .6) - 1) * .7) / 2.4;
    case 5: case 34: return Math.sin(p * 91.7) * Math.cos(p * 37.3);               // noise
    case 18: case 23: return .6 * Math.sin(2 * Math.PI * p) + .3 * Math.sin(4 * Math.PI * p) + .15 * Math.sin(6 * Math.PI * p);
    default: return 2 * frac(p) - 1;                                               // saw-based
  }
}

/** One cycle of the layer's oscillator mix (subtractive / LA), or the engine name. */
export function WaveScreen({ L }: { L: string }) {
  const type = Math.round(pv(`${L}synthType`, 0));
  const wave = Math.round(pv(`${L}wave`, 0));
  const saw = pv(`${L}saw`, 1), pulse = pv(`${L}pulse`, 0), sub = pv(`${L}sub`, 0), pw = pv(`${L}pw`, .5);
  const osc2 = pv(`${L}osc2`, 0), semi = pv(`${L}osc2Semi`, 0);
  const osc2Wave = Math.round(pv(`${L}osc2Wave`, 0));
  const path = useMemo(() => {
    if (type === 5 || type === 6) return '';
    const pts: string[] = [];
    const ratio = Math.pow(2, semi / 12);
    let peak = 1e-3;
    const ys: number[] = [];
    for (let i = 0; i <= 160; i++) {
      const p = i / 80;                                                   // two cycles
      let y = type === 0 ? waveSample(wave, p, pw) * Math.max(saw, .001) : 0;
      if (type !== 0 || wave === 0) y += (2 * frac(p) - 1) * (type === 0 ? 0 : saw);
      y += (frac(p) < pw ? 1 : -1) * pulse * (type === 0 && wave === 1 ? 0 : 1);
      y += (frac(p / 2) < .5 ? 1 : -1) * sub;
      y += waveSample(osc2Wave === 1 ? 1 : osc2Wave === 2 ? 2 : 0, p * ratio, pw) * osc2;
      ys.push(y); peak = Math.max(peak, Math.abs(y));
    }
    ys.forEach((y, i) => pts.push(`${i ? 'L' : 'M'}${(i / 160) * W} ${H / 2 - (y / peak) * (H / 2 - 6)}`));
    return pts.join('');
  }, [type, wave, saw, pulse, sub, pw, osc2, semi, osc2Wave]);
  const info = type === 0 ? SYNTH_WAVES[wave] : SYNTH_SYNTHESIS_TYPES[type];
  return (
    <Screen title="OSC" info={info} className="synth-screen--wave">
      {path ? <path className="synth-screen__line" d={path} /> : <text className="synth-screen__text" x={W / 2} y={H / 2 + 5} textAnchor="middle">{info}</text>}
    </Screen>
  );
}

// ---------------------------------------------------------------- filter
function filterMag(type: number, x: number, q: number): number {
  const d = (qq: number) => Math.sqrt((1 - x * x) ** 2 + (x / qq) ** 2);
  switch (type) {
    case 1: return (x * x) / d(q);                                       // HP12
    case 2: return (x / q) / d(q);                                       // BP12
    case 3: return Math.abs(1 - x * x) / d(q);                           // notch
    case 4: case 5: return (1 / d(.5 + q * .55)) * (1 / d(.54));         // 24 dB LP (ladder / OTA)
    default: return 1 / d(q);                                            // LP12
  }
}

/** Filter response at the knob cutoff, with the envelope's reach dotted. */
export function FilterScreen({ L }: { L: string }) {
  const type = Math.round(pv(`${L}filtType`, 5));
  const cut = pv(`${L}cut`, 2000), q = pv(`${L}res`, .8), env = pv(`${L}filtEnv`, 0);
  const curve = (fc: number) => {
    const pts: string[] = [];
    for (let i = 0; i <= 120; i++) {
      const f = 20 * Math.pow(1000, i / 120);
      const db = 20 * Math.log10(Math.max(filterMag(type, f / fc, Math.max(q, .5)), 1e-4));
      const y = 4 + ((18 - Math.max(-36, Math.min(18, db))) / 54) * (H - 6);   // +18 dB top, -36 dB bottom
      pts.push(`${i ? 'L' : 'M'}${(i / 120) * W} ${y}`);
    }
    return pts.join('');
  };
  const main = curve(cut);
  const reach = Math.abs(env) > .02 ? curve(Math.min(20000, Math.max(20, cut * Math.pow(2, env * 5)))) : '';
  const fmt = cut >= 1000 ? `${(cut / 1000).toFixed(cut >= 10000 ? 0 : 1)}k` : `${Math.round(cut)}`;
  return (
    <Screen title="FILTER" info={`${SYNTH_FILTER_TYPES[type] ?? ''} · ${fmt} Hz`} className="synth-screen--filter">
      {reach && <path className="synth-screen__ghost" d={reach} />}
      <path className="synth-screen__fill" d={`${main}L${W} ${H}L0 ${H}Z`} />
      <path className="synth-screen__line" d={main} />
    </Screen>
  );
}

// ---------------------------------------------------------------- LFO
export function LfoScreen({ n }: { n: number }) {
  const P = `L${n}_`;
  const on = pv(`${P}lfoEnabled`, 0) >= .5;
  const wave = Math.round(pv(`${P}lfoWave`, 0)), rate = pv(`${P}lfoRate`, 2), depth = pv(`${P}lfoDepth`, .5);
  const delay = pv(`${P}lfoDelay`, 0), fade = pv(`${P}lfoFade`, 0);
  const path = useMemo(() => {
    const seconds = 2, pts: string[] = [];
    let held = 0;
    for (let i = 0; i <= 200; i++) {
      const t = (i / 200) * seconds, p = t * Math.min(rate, 6);
      let v: number;
      switch (wave) {
        case 1: v = 1 - 4 * Math.abs(frac(p) - .5); break;
        case 2: v = 2 * frac(p) - 1; break;
        case 3: v = frac(p) < .5 ? 1 : -1; break;
        case 4: if (i === 0 || Math.floor(p) !== Math.floor(p - Math.min(rate, 6) * seconds / 200)) held = Math.sin(Math.floor(p) * 12.9898) * 2 % 1; v = held; break;
        case 5: v = Math.sin(p * 2.1) * .6 + Math.sin(p * 5.3) * .4; break;
        default: v = Math.sin(2 * Math.PI * p);
      }
      const gain = t < delay ? 0 : fade > 0 ? Math.min(1, (t - delay) / fade) : 1;
      pts.push(`${i ? 'L' : 'M'}${(i / 200) * W} ${H / 2 - v * gain * Math.max(.08, depth) * (H / 2 - 5)}`);
    }
    return pts.join('');
  }, [wave, rate, depth, delay, fade]);
  return (
    <Screen title={`LFO ${'ABCD'[n - 1]}`} info={on ? `${SYNTH_LFO_WAVES[wave]} · ${rate.toFixed(2)} Hz` : 'OFF'} className={`synth-screen--lfo${on ? '' : ' is-off'}`}>
      <path className="synth-screen__line" d={path} />
    </Screen>
  );
}
