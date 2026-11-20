import { useEffect, useRef, useState, type ReactNode } from 'react';
import { createPortal } from 'react-dom';

// Open dialogs, innermost last: Escape only closes the top one (a dialog opened from a dialog).
const stack: object[] = [];

/** Accessible dialog: Escape and backdrop click close it; focus moves inside.
 *  Rendered into <body> so no panel stacking context (isolation, transform…) can cover it. */
export function Modal({ title, onClose, children, wide }: { title: string; onClose: () => void; children: ReactNode; wide?: boolean }) {
  const ref = useRef<HTMLDivElement>(null);
  // Callers pass inline onClose arrows (a new function on every render). The effect must
  // run ONCE per opening: re-running it on each keystroke moved the focus to the window
  // and back, so text inputs lost focus after every letter (STORE program name).
  const closeRef = useRef(onClose);
  // The opener, read during the first render (before autoFocus moves the focus inside).
  const [opener] = useState(() => document.activeElement as HTMLElement | null);
  closeRef.current = onClose;
  useEffect(() => {
    // Keep autoFocus on a field inside the dialog; otherwise focus the window itself.
    if (!ref.current?.contains(document.activeElement)) ref.current?.focus();
    const token = {};
    stack.push(token);
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape' && stack[stack.length - 1] === token) closeRef.current(); };
    window.addEventListener('keydown', onKey);
    return () => { window.removeEventListener('keydown', onKey); stack.splice(stack.indexOf(token), 1); opener?.focus?.(); };
  }, [opener]);
  return createPortal(
    <div className="modal" onPointerDown={e => { if (e.target === e.currentTarget) onClose(); }}>
      <div ref={ref} className={`modal__window${wide ? ' modal__window--wide' : ''}`} role="dialog" aria-modal="true" aria-label={title} tabIndex={-1}>
        <header className="modal__head">
          <h2>{title}</h2>
          <button type="button" className="modal__close" aria-label="Cerrar" onClick={onClose}>×</button>
        </header>
        <div className="modal__body">{children}</div>
      </div>
    </div>,
    document.body,
  );
}
