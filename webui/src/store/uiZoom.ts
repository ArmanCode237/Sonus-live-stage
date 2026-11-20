// Zoom nativo de CSS (sin transformaciones que alteren eventos, modales o scroll).
// Se aplica al elemento raiz para que WebView2 y WKWebView recalculen el layout
// como al utilizar Zoom del navegador. Los controles de audio no se modifican.
import { safeUiZoom } from './uiPrefs';

export function applyUiZoom(value: unknown): void {
  const zoom = safeUiZoom(value);
  document.documentElement.style.zoom = String(zoom / 100);
}

export function getUiZoom(): number {
  let saved: unknown;
  try {
    const prefs = JSON.parse(localStorage.getItem('liveStage.uiPrefs') || '{}') as { uiZoom?: unknown };
    saved = prefs.uiZoom;
  } catch { /* datos guardados dañados: utilizar 100 % */ }
  const zoom = safeUiZoom(saved);
  applyUiZoom(zoom);
  return zoom;
}
