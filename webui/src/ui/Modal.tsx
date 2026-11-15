import { useEffect, useRef, type ReactNode } from 'react';
import { createPortal } from 'react-dom';

/** Accessible dialog: Escape and backdrop click close it; focus moves inside.
 *  Rendered into <body> so no panel stacking context (isolation, transform…) can cover it. */
export function Modal({ title, onClose, children, wide }: { title: string; onClose: () => void; children: ReactNode; wide?: boolean }) {
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const previous = document.activeElement as HTMLElement | null;
    ref.current?.focus();
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', onKey);
    return () => { window.removeEventListener('keydown', onKey); previous?.focus?.(); };
  }, [onClose]);
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
