// Small OLED screen that draws an equalizer's real frequency response (eqResponse.ts):
// log frequency grid, dB grid, filled curve, HP/LP corners and numbered band handles.
import { useEffect, useMemo, useRef } from 'react';
import { eqMagnitudeDb, logFrequencies, normalizeEqSlope, type EqModel } from './eqResponse';

const F_LO = 20, F_HI = 20000;
const GRID_HZ = [50, 100, 200, 500, 1000, 2000, 5000, 10000];
const LABEL_HZ: Record<number, string> = { 100: '100', 1000: '1k', 10000: '10k' };
const POINTS = 220;
const FREQS = logFrequencies(POINTS, F_LO, F_HI);

const fmtHz = (hz: number) => (hz >= 1000 ? `${(hz / 1000).toFixed(hz >= 10000 ? 0 : 1)}k` : `${Math.round(hz)}`);

export function EqScreen({ model, title = 'EQ', range = 18, className = '' }: {
  model: EqModel; title?: string; range?: number; className?: string;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const curve = useMemo(() => eqMagnitudeDb(model, FREQS), [model]);
  const bandDb = useMemo(() => eqMagnitudeDb(model, model.bands.map(b => b.freqHz)), [model]);

  useEffect(() => {
    const el = canvas.current;
    if (!el) return;
    const draw = () => {
      const w = el.clientWidth, h = el.clientHeight;
      if (!w || !h) return;
      const dpr = window.devicePixelRatio || 1;
      if (el.width !== Math.round(w * dpr) || el.height !== Math.round(h * dpr)) { el.width = Math.round(w * dpr); el.height = Math.round(h * dpr); }
      const ctx = el.getContext('2d');
      if (!ctx) return;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      const css = getComputedStyle(el);
      const color = (name: string, fallback: string) => css.getPropertyValue(name).trim() || fallback;
      const ink = color('--eq-ink', '#7df9e0'), grid = color('--eq-grid', 'rgba(125,249,224,.12)');
      const dim = color('--eq-dim', 'rgba(125,249,224,.45)'), fill = color('--eq-fill', 'rgba(125,249,224,.18)');
      const handle = color('--eq-handle', '#ffb547');
      const padT = 4, padB = 12;
      const x = (hz: number) => (Math.log(hz / F_LO) / Math.log(F_HI / F_LO)) * w;
      const y = (db: number) => padT + (1 - (Math.max(-range, Math.min(range, db)) + range) / (2 * range)) * (h - padT - padB);
      ctx.clearRect(0, 0, w, h);

      // grid
      ctx.lineWidth = 1; ctx.strokeStyle = grid; ctx.fillStyle = dim;
      ctx.font = '600 8px ui-monospace, Consolas, monospace'; ctx.textBaseline = 'alphabetic';
      for (const hz of GRID_HZ) {
        const gx = Math.round(x(hz)) + .5;
        ctx.beginPath(); ctx.moveTo(gx, padT); ctx.lineTo(gx, h - padB); ctx.stroke();
        if (LABEL_HZ[hz]) { ctx.textAlign = 'center'; ctx.fillText(LABEL_HZ[hz], gx, h - 2); }
      }
      for (const db of [-12, -6, 6, 12].filter(d => Math.abs(d) < range)) {
        const gy = Math.round(y(db)) + .5;
        ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(w, gy); ctx.stroke();
      }
      ctx.textAlign = 'left';
      ctx.fillText(`+${range}`, 3, padT + 8); ctx.fillText(`-${range}`, 3, h - padB - 2);
      const zero = Math.round(y(0)) + .5;
      ctx.strokeStyle = dim; ctx.setLineDash([2, 3]);
      ctx.beginPath(); ctx.moveTo(0, zero); ctx.lineTo(w, zero); ctx.stroke(); ctx.setLineDash([]);

      // HP / LP corners
      const corner = (hz: number | undefined, slope: number | undefined, label: string, active: boolean) => {
        if (!active || hz === undefined) return;
        const cx = Math.round(x(hz)) + .5;
        ctx.strokeStyle = dim; ctx.setLineDash([1, 2]);
        ctx.beginPath(); ctx.moveTo(cx, padT); ctx.lineTo(cx, h - padB); ctx.stroke(); ctx.setLineDash([]);
        ctx.fillStyle = dim; ctx.textAlign = label === 'HP' ? 'left' : 'right';
        ctx.fillText(`${label} ${normalizeEqSlope(slope)}`, cx + (label === 'HP' ? 3 : -3), padT + 18);
      };
      corner(model.hpHz, model.hpSlope, 'HP', model.enabled && (model.hpHz ?? 0) > 22);
      corner(model.lpHz, model.lpSlope, 'LP', model.enabled && (model.lpHz ?? 20000) < 19500);

      // filled curve
      ctx.beginPath();
      curve.forEach((db, i) => { const px = (i / (POINTS - 1)) * w; if (i) ctx.lineTo(px, y(db)); else ctx.moveTo(px, y(db)); });
      ctx.lineTo(w, zero); ctx.lineTo(0, zero); ctx.closePath();
      ctx.fillStyle = model.enabled ? fill : 'transparent'; ctx.fill();
      ctx.beginPath();
      curve.forEach((db, i) => { const px = (i / (POINTS - 1)) * w; if (i) ctx.lineTo(px, y(db)); else ctx.moveTo(px, y(db)); });
      ctx.strokeStyle = model.enabled ? ink : dim; ctx.lineWidth = 1.8;
      ctx.shadowColor = model.enabled ? ink : 'transparent'; ctx.shadowBlur = 6; ctx.stroke(); ctx.shadowBlur = 0;

      // band handles
      if (model.enabled) model.bands.forEach((b, i) => {
        const bx = x(Math.max(F_LO, Math.min(F_HI, b.freqHz))), by = y(bandDb[i] ?? 0);
        ctx.beginPath(); ctx.arc(bx, by, 5.5, 0, Math.PI * 2);
        ctx.fillStyle = '#000'; ctx.fill(); ctx.lineWidth = 1.4; ctx.strokeStyle = handle; ctx.stroke();
        ctx.fillStyle = handle; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
        ctx.font = '800 8px ui-monospace, Consolas, monospace'; ctx.fillText(String(i + 1), bx, by + .5);
        ctx.textBaseline = 'alphabetic'; ctx.font = '600 8px ui-monospace, Consolas, monospace';
      });
    };
    draw();
    const ro = new ResizeObserver(draw);
    ro.observe(el);
    return () => ro.disconnect();
  }, [curve, bandDb, model, range]);

  const peak = model.bands.reduce<{ i: number; g: number } | null>((best, b, i) => (Math.abs(b.gainDb) > (best ? Math.abs(best.g) : 0.05) ? { i, g: b.gainDb } : best), null);
  return (
    <div className={`eq-screen${model.enabled ? '' : ' is-bypassed'} ${className}`}>
      <div className="eq-screen__bar">
        <span>{title}</span>
        <span>{!model.enabled ? 'BYPASS'
          : peak ? `B${peak.i + 1} ${peak.g > 0 ? '+' : ''}${peak.g.toFixed(1)} dB @ ${fmtHz(model.bands[peak.i].freqHz)}`
          : 'FLAT'}</span>
      </div>
      <canvas ref={canvas} className="eq-screen__canvas" role="img"
        aria-label={`Curva de ecualización ${title}${model.enabled ? '' : ' (bypass)'}`} />
    </div>
  );
}
