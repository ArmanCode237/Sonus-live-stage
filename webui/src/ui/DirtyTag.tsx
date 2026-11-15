// "● EDITADO": the module has edits (sound or its MIDI / OSC mappings) that are not
// saved in its preset yet. Cleared when the module preset is saved or another is loaded.
import { useEngine } from '../store/hooks';

export function DirtyTag({ module }: { module: 0 | 1 | 2 }) {
  const dirty = useEngine(s => s?.moduleDirty?.[module] ?? false);
  return dirty ? <span className="dirty-tag" title="Cambios sin guardar en el preset de este módulo">● EDITADO</span> : null;
}
