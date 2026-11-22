// A render error inside one view must never blank the whole console: the header,
// navigation and PANIC stay usable, and the view can be retried. The engine keeps
// playing either way (it does not depend on the page).
import { Component, type ErrorInfo, type ReactNode } from 'react';

interface State { error: Error | null }

export class ViewBoundary extends Component<{ children: ReactNode }, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State { return { error }; }

  componentDidCatch(error: Error, info: ErrorInfo) {
    console.error('[UI] Error en la vista:', error, info.componentStack);
  }

  render() {
    const { error } = this.state;
    if (!error) return this.props.children;
    return (
      <div className="view-loading" role="alert" style={{ flexDirection: 'column', gap: 12, textAlign: 'center' }}>
        <strong>Esta sección encontró un error y se detuvo.</strong>
        <span className="muted">El motor de audio sigue funcionando. Detalle: {error.message}</span>
        <button type="button" className="btn btn--panel" onClick={() => this.setState({ error: null })}>REINTENTAR</button>
      </div>
    );
  }
}
