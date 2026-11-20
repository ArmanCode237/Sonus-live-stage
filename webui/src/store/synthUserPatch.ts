// v32.5: the synth / organ preset that is playing, as the ENGINE knows it (state.presetRefs).
// It survives page reloads, is the same in every window (native, browser, tablet) and is
// stored with each Program. null = a factory synth patch (shown by its number) / no name.
import type { PresetRef } from '../api/types';
import { useEngine } from './hooks';

const pick = (ref: PresetRef | null | undefined): PresetRef | null => (ref && typeof ref.name === 'string' && ref.name ? ref : null);

/** Synth preset name + kind ('USER' | 'D-50'); null when a factory patch is playing. */
export function useSynthPresetRef(): PresetRef | null {
  const name = useEngine(s => pick(s?.presetRefs?.synth)?.name ?? '');
  const kind = useEngine(s => pick(s?.presetRefs?.synth)?.kind ?? '');
  return name ? { name, kind } : null;
}

/** Organ preset name + kind ('FACTORY' | 'USER'); null when unknown (manual registration). */
export function useOrganPresetRef(): PresetRef | null {
  const name = useEngine(s => pick(s?.presetRefs?.organ)?.name ?? '');
  const kind = useEngine(s => pick(s?.presetRefs?.organ)?.kind ?? '');
  return name ? { name, kind } : null;
}
