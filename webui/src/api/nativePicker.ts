/** Explorador del sistema. Solo existe en la ventana nativa del standalone.
 * No se usa <input type=file> para rutas: WebView no ofrece una ruta absoluta
 * fiable al proceso C++ y la API de samples requiere esa ruta local. */
export type PickerKind = 'folder' | 'file';
declare global {
  interface Window { __liveStageSelectSamplePath?: (kind: PickerKind) => Promise<string>; }
}
export const hasNativeSamplePicker = () => typeof window.__liveStageSelectSamplePath === 'function';
export async function chooseSamplePath(kind: PickerKind): Promise<string | null> {
  if (!hasNativeSamplePicker()) return null;
  const path = await window.__liveStageSelectSamplePath!(kind);
  return typeof path === 'string' && path.length > 0 ? path : null;
}
