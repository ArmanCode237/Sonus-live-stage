// Sound sources of each module for the sound browser. Loading always goes through the
// module's existing routes: the browser only changes how sounds are found and organised.
import { useCallback, useEffect, useMemo, useState } from 'react';
import { api } from '../api/client';
import type { D50Bank, Dx7Bank, LibraryItem, Mt32Status, NamedPreset, SoundSummary } from '../api/types';
import { SYNTH_SYNTHESIS_TYPES } from '../data/constants';
import { SYNTH_FACTORY } from '../data/synthFactory';
import { SYNTH_FACTORY_ENGINES } from '../data/synthFactoryEngines';
import { useEngine } from '../store/hooks';
import { loadLibrary, patchLibraryItem, useLibrary } from '../store/library';
import { runAction } from '../store/notify';
import { displayName, isEdited, restoreFactoryOriginal, toggleFavorite, useFactoryMeta } from '../store/synthFactory';
import { useOrganPresetRef, useSynthPresetRef } from '../store/synthUserPatch';
import type { BrowserItem, SubItem } from './model';
import { forgetKey, renameKey } from './meta';

export interface Provider { items: BrowserItem[]; loading: boolean; activeKey: string | null; reload: () => void }

const ok = (r: unknown) => Boolean(r);
const engineName = (types: readonly number[]) => types.map(t => SYNTH_SYNTHESIS_TYPES[t] ?? '?').join(' + ');
const stripExt = (n: string) => n.replace(/\.[^.]+$/, '');

/** Engines of a stored synth patch (its enabled layers). */
function patchEngines(params: unknown): string | undefined {
  const layers = (params as { layers?: { enabled?: boolean; synthesisType?: number }[] } | undefined)?.layers;
  if (!Array.isArray(layers)) return undefined;
  const types = [...new Set(layers.filter(l => l?.enabled).map(l => Math.round(l.synthesisType ?? 0)))].sort();
  return types.length ? engineName(types) : undefined;
}

// ------------------------------------------------------------------ synth
export function useSynthSources(): Provider {
  const { favorites, overrides } = useFactoryMeta();
  const presetId = Math.round(useEngine(s => s?.params?.synthPresetId ?? 1));
  const user = useSynthPresetRef();
  const [userPresets, setUserPresets] = useState<(NamedPreset & { params?: unknown })[]>([]);
  const [dx7, setDx7] = useState<Dx7Bank[]>([]);
  const [d50, setD50] = useState<D50Bank[]>([]);
  const [mt32, setMt32] = useState<Mt32Status | null>(null);
  const [loading, setLoading] = useState(true);
  const reload = useCallback(() => {
    setLoading(true);
    void Promise.allSettled([
      api.synthPresets().then(r => setUserPresets(r as (NamedPreset & { params?: unknown })[])),
      api.dx7Banks().then(r => setDx7(r.banks)),
      api.d50Banks().then(r => setD50(r.banks)),
      api.mt32Status().then(setMt32),
    ]).finally(() => setLoading(false));
  }, []);
  useEffect(reload, [reload]);

  const items = useMemo<BrowserItem[]>(() => {
    const out: BrowserItem[] = [];
    for (const p of SYNTH_FACTORY) {
      const edited = isEdited(p.id, overrides);
      out.push({
        key: `synth:f:${p.id}`, module: 'synth', origin: edited ? 'edited' : 'factory', name: displayName(p, overrides),
        number: String(p.id).padStart(3, '0'), baseCategory: p.category, engine: engineName(SYNTH_FACTORY_ENGINES[p.id] ?? [0]),
        detail: p.category, sourceFavorite: favorites.has(p.id), toggleSourceFavorite: () => toggleFavorite(p.id),
        info: [['Preset', `${String(p.id).padStart(3, '0')} / ${SYNTH_FACTORY.length}`], ...(edited ? [['Original', p.name] as [string, string]] : [])],
        load: async () => ok(await runAction(() => api.loadSynthFactory(p.id), displayName(p, overrides), true)),
        restore: edited ? () => restoreFactoryOriginal(p.id) : undefined, restoreLabel: 'RESTAURAR ORIGINAL',
      });
    }
    for (const u of userPresets) {
      const basedOn = Math.round((u.params as { synthPresetId?: number } | undefined)?.synthPresetId ?? 0);
      const base = SYNTH_FACTORY[basedOn - 1];
      const key = `synth:u:${u.name}`;
      out.push({
        key, module: 'synth', origin: 'user', name: u.name, baseCategory: base?.category ?? 'USUARIO',
        engine: patchEngines(u.params), detail: patchEngines(u.params),
        info: base ? [['Basado en', base.name]] : [],
        load: async () => ok(await runAction(() => api.loadSynthPreset(u.name), `«${u.name}»`, true)),
        rename: async to => { const r = await runAction(() => api.renameSynthPreset(u.name, to), `Renombrado a «${to}»`); if (r) { await renameKey(key, `synth:u:${to}`); reload(); } return ok(r); },
        duplicate: async to => { const r = await runAction(() => api.duplicateSynthPreset(u.name, to), `Duplicado como «${to}»`); if (r) reload(); return ok(r); },
        remove: async () => { const r = await runAction(() => api.deleteSynthPreset(u.name), `«${u.name}» eliminado`); if (r) { await forgetKey(key); reload(); } return ok(r); },
      });
    }
    for (const bank of dx7) {
      bank.voices.forEach((voice, i) => out.push({
        key: `synth:dx7:${bank.id}:${i}`, module: 'synth', origin: 'dx7', name: voice.trim() || `Voz ${i + 1}`,
        number: String(i + 1).padStart(2, '0'), baseCategory: 'DX7', engine: 'DX7', needsLayer: true,
        detail: `${bank.source === 'factory' ? '' : 'USUARIO · '}${bank.name}`, info: [['Cartucho', bank.name], ['Voz', String(i + 1)]],
        load: async ctx => ok(await runAction(() => api.dx7Load(ctx.layer, bank.id, i), `DX7 · ${voice.trim()} → capa ${'ABCD'[ctx.layer]}`, true)),
      }));
    }
    for (const bank of d50) {
      for (const p of bank.patches) {
        const n = `${Math.floor((p.n - 1) / 8) + 1}${((p.n - 1) % 8) + 1}`;
        out.push({
          key: `synth:d50:${bank.id}:${p.n}`, module: 'synth', origin: 'd50', name: p.name, number: n, baseCategory: 'D-50', engine: 'LA32',
          detail: `${bank.name} · ${p.mode}`, info: [['Banco', bank.name], ['Modo', p.mode], ['Upper', p.upper], ...(p.mode !== 'WHOLE' ? [['Lower', p.lower] as [string, string]] : [])],
          load: async () => ok(await runAction(() => api.d50Load(bank.id, p.n), `D-50 · ${p.name}`, true)),
        });
      }
    }
    if (mt32?.loaded) {
      mt32.sounds.forEach((name, i) => out.push({
        key: `synth:mt32:${i}`, module: 'synth', origin: 'mt32', name, number: `${i < 64 ? 'A' : 'B'}${String((i % 64) + 1).padStart(2, '0')}`,
        baseCategory: 'MT-32', engine: 'MT-32', needsLayer: true, detail: 'ROLAND MT-32',
        load: async ctx => ok(await runAction(() => api.mt32Load(ctx.layer, i), `MT-32 · ${name} → capa ${'ABCD'[ctx.layer]}`, true)),
      }));
    }
    return out;
  }, [favorites, overrides, userPresets, dx7, d50, mt32, reload]);

  const activeKey = user?.kind === 'USER' ? `synth:u:${user.name}` : user ? null : `synth:f:${presetId}`;
  return { items, loading, activeKey, reload };
}

// ------------------------------------------------------------------ organ
const ORGAN_CATEGORIES: [RegExp, string][] = [
  [/jazz|smith|groove/i, 'JAZZ'], [/gospel|church|pipe|cathedral|choir/i, 'GOSPEL / CHURCH'], [/rock|lord|purple|overdrive|drive/i, 'ROCK'],
  [/vox|farfisa|combo|transistor|60s|sixties/i, 'COMBO'], [/ballad|soft|mellow|full|stops/i, 'BALLAD / FULL'],
];
const organCategory = (name: string) => ORGAN_CATEGORIES.find(([re]) => re.test(name))?.[1] ?? 'ORGAN';

export function useOrganSources(): Provider {
  const [factory, setFactory] = useState<{ factoryId: number; name: string }[]>([]);
  const [user, setUser] = useState<NamedPreset[]>([]);
  const [loading, setLoading] = useState(true);
  const loaded = useOrganPresetRef();
  const reload = useCallback(() => {
    setLoading(true);
    void Promise.allSettled([api.organFactory().then(setFactory), api.organPresets().then(setUser)]).finally(() => setLoading(false));
  }, []);
  useEffect(reload, [reload]);
  const items = useMemo<BrowserItem[]>(() => [
    ...factory.map((f): BrowserItem => ({
      key: `organ:f:${f.factoryId}`, module: 'organ', origin: 'factory', name: f.name, number: String(f.factoryId).padStart(2, '0'),
      baseCategory: organCategory(f.name), engine: 'ORGAN',
      load: async () => { const r = await runAction(() => api.loadOrganFactory(f.factoryId), f.name, true); return ok(r); },
    })),
    ...user.map((u): BrowserItem => {
      const key = `organ:u:${u.name}`;
      return {
        key, module: 'organ', origin: 'user', name: u.name, baseCategory: organCategory(u.name), engine: 'ORGAN',
        load: async () => { const r = await runAction(() => api.loadOrganPreset(u.name), `«${u.name}»`, true); return ok(r); },
        rename: async to => { const r = await runAction(() => api.renameOrganPreset(u.name, to), `Renombrado a «${to}»`); if (r) { await renameKey(key, `organ:u:${to}`); reload(); } return ok(r); },
        duplicate: async to => { const r = await runAction(() => api.duplicateOrganPreset(u.name, to), `Duplicado como «${to}»`); if (r) reload(); return ok(r); },
        remove: async () => { const r = await runAction(() => api.deleteOrganPreset(u.name), `«${u.name}» eliminado`); if (r) { await forgetKey(key); reload(); } return ok(r); },
      };
    }),
  ], [factory, user, reload]);
  const activeKey = loaded ? items.find(i => i.name === loaded.name && i.origin === (loaded.kind === 'USER' ? 'user' : 'factory'))?.key ?? null : null;
  return { items, loading, activeKey, reload };
}

// ------------------------------------------------------------------ piano
export function usePianoSources(layer: number): Provider {
  const { items: library } = useLibrary();
  const fileName = useEngine(s => s?.pianoLayers?.[layer]?.fileName ?? '');
  const [sounds, setSounds] = useState<SoundSummary[]>([]);
  const [perfs, setPerfs] = useState<{ name: string; layerCount?: number; legacy?: boolean }[]>([]);
  const [loading, setLoading] = useState(true);
  const reload = useCallback(() => {
    setLoading(true);
    void Promise.allSettled([
      loadLibrary(true),
      api.sounds('piano').then(r => setSounds(r.sounds)),
      api.pianoPerformances().then(r => setPerfs(r.performances)),
    ]).finally(() => setLoading(false));
  }, []);
  useEffect(() => {
    setLoading(true);
    void Promise.allSettled([api.sounds('piano').then(r => setSounds(r.sounds)), api.pianoPerformances().then(r => setPerfs(r.performances))])
      .finally(() => setLoading(false));
  }, []);

  const items = useMemo<BrowserItem[]>(() => {
    const out: BrowserItem[] = [];
    for (const item of library ?? []) {
      const format = item.ext.replace('.', '').toUpperCase();
      const loadInto = (preset: number) => async (ctx: { layer: number }) =>
        ok(await runAction(() => api.loadSample(item.path, ctx.layer, preset), `L${ctx.layer + 1}: ${stripExt(item.name)}`, true));
      out.push({
        key: `piano:lib:${item.path}`, module: 'piano', origin: 'sample', name: stripExt(item.name),
        baseCategory: item.group || item.category || 'BIBLIOTECA', engine: format, needsLayer: true,
        detail: `${format} · ${item.sizeMB.toFixed(0)} MB${item.ready ? '' : ' · sin caché'}`,
        info: [['Archivo', item.name], ['Formato', format], ['Tamaño', `${item.sizeMB.toFixed(0)} MB`], ['Caché', item.ready ? 'lista' : 'se prepara al cargar']],
        sourceFavorite: item.favorite,
        toggleSourceFavorite: async () => {
          const favorite = !item.favorite;
          patchLibraryItem(item.path, { favorite });
          await runAction(() => api.tagLibrary(item.name, { favorite }));
        },
        load: loadInto(item.ext.toLowerCase() === '.sf2' ? 0 : -1),
        subItems: item.ext.toLowerCase() === '.sf2' ? async (): Promise<SubItem[] | null> => {
          try {
            const r = await api.sf2Presets(item.path);
            return r.presets.length > 1 ? r.presets.map((label, i) => ({ label: `${String(i).padStart(3, '0')} · ${label}`, load: loadInto(i) })) : null;
          } catch { return null; }
        } : undefined,
      });
    }
    for (const s of sounds) {
      const key = `piano:snd:${s.id}`;
      out.push({
        key, module: 'piano', origin: 'sound', name: s.name, baseCategory: s.category || 'SOUNDS', baseTags: s.tags ?? [],
        engine: 'SOUND', needsLayer: true, detail: (s.instruments ?? []).slice(0, 2).join(', '),
        info: [['Instrumentos', (s.instruments ?? []).join(', ') || '—'], ...(s.updated ? [['Modificado', new Date(s.updated).toLocaleString()] as [string, string]] : [])],
        load: async ctx => ok(await runAction(() => api.loadSound(s.id, ctx.layer), `L${ctx.layer + 1}: ${s.name}`, true)),
        duplicate: async to => { const r = await runAction(() => api.duplicateSound(s.id, to), `Duplicado como «${to}»`); if (r) reload(); return ok(r); },
        remove: async () => { const r = await runAction(() => api.deleteSound(s.id), `«${s.name}» eliminado`); if (r) { await forgetKey(key); reload(); } return ok(r); },
      });
    }
    for (const p of perfs) {
      const key = `piano:perf:${p.name}`;
      out.push({
        key, module: 'piano', origin: 'performance', name: p.name, baseCategory: 'PERFORMANCES', engine: 'PERFORMANCE',
        detail: p.layerCount ? `${p.layerCount} capas` : 'PERFORMANCE', info: [['Capas', String(p.layerCount ?? '—')]],
        load: async () => ok(await runAction(() => api.loadPianoPerformance(p.name), `Performance «${p.name}»`, true)),
        remove: async () => { const r = await runAction(() => api.deletePianoPerformance(p.name), `«${p.name}» eliminada`); if (r) { await forgetKey(key); reload(); } return ok(r); },
      });
    }
    return out;
  }, [library, sounds, perfs, reload]);

  const active = (library ?? []).find((i: LibraryItem) => i.name === fileName);
  return { items, loading: loading || !library, activeKey: active ? `piano:lib:${active.path}` : null, reload };
}
