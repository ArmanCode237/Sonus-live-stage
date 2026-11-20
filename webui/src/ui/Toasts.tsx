import { useToasts } from '../store/notify';

export function Toasts() {
  const toasts = useToasts();
  return (
    <div className="toasts" role="status" aria-live="polite">
      {toasts.map(t => <div key={t.id} className={`toast toast--${t.kind}`}>{t.text}</div>)}
    </div>
  );
}
