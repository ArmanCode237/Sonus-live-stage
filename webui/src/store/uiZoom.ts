// Escala de la interfaz.
//
// Modelo: la consola se diseña sobre un lienzo de referencia de 1280×800 px CSS.
//   AUTO encendido  → zoom = ajuste(ventana / referencia) × tamaño elegido
//   AUTO apagado    → zoom = tamaño elegido
// El resultado se limita para que el lienzo efectivo nunca sea menor que 1180×780: por
// debajo de eso las vistas pasan a una sola columna y los paneles dejan de caber.
//
// Aplicación:
//   1. Zoom NATIVO del WebView (WKWebView.pageZoom en macOS, ZoomFactor en WebView2): el
//      motor recalcula el layout igual que el zoom del navegador; media queries, vh, eventos
//      del ratón y menús flotantes quedan coherentes. Lo expone el host C++ como
//      window.__liveStageSetZoom(z) → z aplicado (o la consulta del zoom actual con null).
//   2. Fallback (navegador, plug-in): `zoom` CSS en <html>. Las medidas de viewport que
//      usan los estilos (--app-w/--app-h/--vw/--vh) se publican aquí ya compensadas.
import { useSyncExternalStore } from 'react';
import { getUiPrefs, setUiPref, stepUiScale, subscribeUiPrefs } from './uiPrefs';

export const UI_REF_W = 1280;
export const UI_REF_H = 800;
const MIN_W = 1180, MIN_H = 780;          // lienzo efectivo mínimo
const MIN_ZOOM = .6, MAX_ZOOM = 3;

type NativeZoom = (z: number | null) => Promise<unknown>;
declare global { interface Window { __liveStageSetZoom?: NativeZoom } }

export interface UiScaleInfo {
  /** Zoom total aplicado (1 = 100 %). */
  zoom: number;
  /** Factor de ajuste AUTO a la ventana (1 si AUTO está apagado). */
  fit: number;
  /** true si lo aplica el WebView nativo. */
  native: boolean;
  /** true si el tamaño pedido se recortó por falta de espacio. */
  limited: boolean;
  /** Lienzo efectivo de la consola en px CSS. */
  width: number;
  height: number;
}

let native: NativeZoom | null = null;
let applied = 1;                 // zoom vigente (nativo o CSS)
let info: UiScaleInfo = { zoom: 1, fit: 1, native: false, limited: false, width: UI_REF_W, height: UI_REF_H };
const listeners = new Set<() => void>();
let frame = 0;

/** Tamaño de la ventana en px a zoom 1, independiente del zoom aplicado. */
function physicalSize(): { w: number; h: number } {
  const w = Math.max(1, window.innerWidth), h = Math.max(1, window.innerHeight);
  // El zoom nativo reduce innerWidth/innerHeight; el zoom CSS no los modifica.
  return native ? { w: w * applied, h: h * applied } : { w, h };
}

function compute(): Omit<UiScaleInfo, 'width' | 'height' | 'native'> {
  const { uiAutoFit, uiScale } = getUiPrefs();
  const { w, h } = physicalSize();
  const fit = uiAutoFit ? Math.min(w / UI_REF_W, h / UI_REF_H) : 1;
  const wanted = fit * uiScale / 100;
  const roomCap = Math.min(w / MIN_W, h / MIN_H);
  let zoom = Math.min(wanted, roomCap);
  zoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, zoom));
  zoom = Math.round(zoom * 100) / 100;
  return { zoom, fit, limited: wanted > roomCap + .005 };
}

function publishCssMetrics(zoom: number): void {
  const { w, h } = physicalSize();
  // Con zoom nativo el viewport CSS ya está escalado (innerWidth); con zoom CSS hay que dividir.
  const cssW = native ? window.innerWidth : w / zoom;
  const cssH = native ? window.innerHeight : h / zoom;
  const root = document.documentElement.style;
  root.setProperty('--app-w', `${cssW.toFixed(2)}px`);
  root.setProperty('--app-h', `${cssH.toFixed(2)}px`);
  root.setProperty('--vw', `${(cssW / 100).toFixed(3)}px`);
  root.setProperty('--vh', `${(cssH / 100).toFixed(3)}px`);
  root.setProperty('--ui-zoom', String(zoom));
  info = { ...info, width: Math.round(cssW), height: Math.round(cssH) };
}

function emit(): void { listeners.forEach(l => l()); }

function applyCss(zoom: number): void {
  document.documentElement.style.zoom = zoom === 1 ? '' : String(zoom);
}

/** Recalcula y aplica la escala (preferencias o tamaño de ventana cambiados). */
export function refreshUiZoom(): void {
  const next = compute();
  if (Math.abs(next.zoom - applied) < .011) {
    // Sin cambio de zoom: solo actualizar las métricas (p. ej. cambió la proporción de la ventana).
    info = { ...info, ...next, zoom: applied, native: !!native };
    publishCssMetrics(applied);
    emit();
    return;
  }
  const previous = applied;
  applied = next.zoom;
  info = { ...info, ...next, native: !!native };
  if (native) {
    const fallback = () => { native = null; applied = 1; applyCss(1); refreshUiZoom(); };
    native(next.zoom).then(result => {
      // El host no pudo aplicar el zoom nativo (p. ej. macOS < 11): usar el fallback CSS.
      if (!(typeof result === 'number' && result > 0)) { applied = previous; fallback(); }
    }).catch(fallback);
    publishCssMetrics(next.zoom);
  } else {
    applyCss(next.zoom);
    publishCssMetrics(next.zoom);
  }
  emit();
}

/** Convierte coordenadas de evento (clientX/Y) a px CSS de los elementos `position: fixed`. */
export function eventToCssPx(v: number): number {
  return native ? v : v / applied;
}

function schedule(): void {
  if (frame) return;
  frame = requestAnimationFrame(() => { frame = 0; refreshUiZoom(); });
}

function onKey(e: KeyboardEvent): void {
  if (!(e.metaKey || e.ctrlKey) || e.altKey) return;
  const { uiScale } = getUiPrefs();
  if (e.key === '+' || e.key === '=') { e.preventDefault(); setUiPref('uiScale', stepUiScale(uiScale, 1)); }
  else if (e.key === '-' || e.key === '_') { e.preventDefault(); setUiPref('uiScale', stepUiScale(uiScale, -1)); }
  else if (e.key === '0') { e.preventDefault(); setUiPref('uiScale', 100); }
}

let installed = false;
/** Se llama una vez antes del primer render. */
export function installUiZoom(): void {
  if (installed) return;
  installed = true;
  native = typeof window.__liveStageSetZoom === 'function' ? window.__liveStageSetZoom : null;
  // Primer pintado: métricas correctas aunque el zoom nativo aún no se haya consultado.
  info = { ...info, native: !!native };
  if (native) {
    publishCssMetrics(1);
    // El WebView conserva su zoom tras recargar la página: consultarlo antes de calcular.
    native(null).then(current => {
      if (typeof current === 'number' && current > 0) applied = current;
      refreshUiZoom();
    }).catch(() => { native = null; refreshUiZoom(); });
  } else {
    refreshUiZoom();
  }
  window.addEventListener('resize', schedule);
  window.addEventListener('keydown', onKey, true);
  subscribeUiPrefs(schedule);
}

export function useUiScaleInfo(): UiScaleInfo {
  return useSyncExternalStore(cb => { listeners.add(cb); return () => { listeners.delete(cb); }; }, () => info);
}
