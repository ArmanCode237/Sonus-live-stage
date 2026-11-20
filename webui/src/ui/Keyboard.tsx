// 88-key visualizer (A0–C8). Shows notes held by MIDI (red) or by the sustain
// pedal (amber); clicking a key plays it through the engine.
import { memo, useMemo, useRef } from 'react';
import { engine } from '../store/engine';
import { useEngine } from '../store/hooks';

const FIRST = 21, LAST = 108;
const BLACK = new Set([1, 3, 6, 8, 10]);
const isBlack = (n: number) => BLACK.has(n % 12);

const layout = (() => {
  const whites: number[] = [];
  for (let n = FIRST; n <= LAST; n++) if (!isBlack(n)) whites.push(n);
  const index = new Map(whites.map((n, i) => [n, i]));
  const blacks: { note: number; left: number }[] = [];
  for (let n = FIRST; n <= LAST; n++) if (isBlack(n)) blacks.push({ note: n, left: (index.get(n - 1)! + 1) / whites.length });
  return { whites, blacks, count: whites.length };
})();

const keysSignature = (keys: { k: number; s: number }[] | undefined) => (keys ?? []).map(k => `${k.k}:${k.s}`).join(',');

export const Keyboard = memo(function Keyboard() {
  const signature = useEngine(s => keysSignature(s?.activeKeys));
  const held = useMemo(() => {
    const map = new Map<number, number>();
    if (signature) for (const part of signature.split(',')) { const [k, s] = part.split(':').map(Number); map.set(k, s); }
    return map;
  }, [signature]);
  const down = useRef(new Set<number>());
  const press = (note: number) => { if (!down.current.has(note)) { down.current.add(note); engine.note(note, true, 100); } };
  const release = (note: number) => { if (down.current.delete(note)) engine.note(note, false); };
  const cls = (note: number) => { const s = held.get(note); return s === undefined ? '' : s === 1 ? ' is-down' : ' is-sustained'; };
  const handlers = (note: number) => ({
    onPointerDown: (e: React.PointerEvent) => { e.currentTarget.releasePointerCapture?.(e.pointerId); press(note); },
    onPointerUp: () => release(note), onPointerLeave: () => release(note), onPointerCancel: () => release(note),
  });
  return (
    <div className="keyboard" aria-label="Teclado de 88 teclas">
      <div className="keyboard__keys">
        {layout.whites.map(n => (
          <div key={n} className={`key key--white${cls(n)}`} {...handlers(n)}>
            {n % 12 === 0 && <span className="key__label">C{n / 12 - 1}</span>}
          </div>
        ))}
        {layout.blacks.map(({ note, left }) => (
          <div key={note} className={`key key--black${cls(note)}`} style={{ left: `calc(${left * 100}% - var(--black-w) / 2)` }} {...handlers(note)} />
        ))}
      </div>
    </div>
  );
});
