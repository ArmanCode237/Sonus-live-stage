// Typed access to every C++ endpoint. One place for timeouts, JSON handling and
// error reporting; views never call fetch() directly.
import type {
  ControlState, EngineParams, EngineState, FactoryMeta, HardwareStatus, Json, LearnState, MappingState, LibraryItem, NamedPreset,
  PianoLayerSource, ProgramSlot, SampleLibraryStatus, SoundSummary, Dx7Bank, Mt32Status, D50Bank } from './types';
import type { StudioRack } from '../data/studio';

export class ApiError extends Error {
  constructor(message: string, readonly status: number, readonly body?: unknown) { super(message); }
}

const DEFAULT_TIMEOUT_MS = 8000;
/** Loading large sample banks can take a while on a cold cache. */
const LOAD_TIMEOUT_MS = 120000;

async function request<T>(method: 'GET' | 'POST', url: string, body?: unknown, timeoutMs = DEFAULT_TIMEOUT_MS): Promise<T> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(url, {
      method,
      cache: 'no-store',
      signal: controller.signal,
      headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const text = await response.text();
    let data: unknown = undefined;
    if (text) { try { data = JSON.parse(text); } catch { data = text; } }
    if (!response.ok) {
      const detail = data && typeof data === 'object' ? (data as Json).error : undefined;
      throw new ApiError(detail ? String(detail) : `HTTP ${response.status}`, response.status, data);
    }
    if (data && typeof data === 'object' && (data as Json).ok === false) {
      throw new ApiError(String((data as Json).error || (data as Json).message || 'Operación rechazada'), response.status, data);
    }
    return data as T;
  } catch (error) {
    if (error instanceof ApiError) throw error;
    if ((error as Error).name === 'AbortError') throw new ApiError('Sin respuesta del motor (tiempo agotado)', 0);
    throw new ApiError('No se puede conectar con el motor de audio', 0);
  } finally {
    clearTimeout(timer);
  }
}
const get = <T>(url: string, timeoutMs?: number) => request<T>('GET', url, undefined, timeoutMs);
const post = <T = Json>(url: string, body: unknown = {}, timeoutMs?: number) => request<T>('POST', url, body, timeoutMs);
const q = (value: string) => encodeURIComponent(value);

type WithParams = { params?: EngineParams; pianoLayers?: PianoLayerSource[] };

export const api = {
  // ---- live state & parameters -------------------------------------------
  state: () => get<EngineState>('/api/state', 4000),
  setParam: (id: string, value: number) => post<{ ok: boolean; params?: EngineParams }>('/api/param', { id, value }),
  note: (key: number, on: boolean, vel = 100) => post('/api/note', { key, on, vel }),
  testTone: () => post('/api/testtone'),

  // ---- global programs -----------------------------------------------------
  programs: () => get<ProgramSlot[]>('/api/programs'),
  storeProgram: (slot: number, name?: string) =>
    post<{ programs: ProgramSlot[]; programName: string }>('/api/programs/store', name ? { slot, name } : { slot }),
  recallProgram: (slot: number) => post<WithParams & { programName: string }>('/api/programs/recall', { slot }, LOAD_TIMEOUT_MS),
  initProgram: () => post<WithParams>('/api/programs/init'),
  importPrograms: (programs: unknown) => post<{ programs: ProgramSlot[] }>('/api/programs/import', programs),

  // ---- piano ---------------------------------------------------------------
  loadSample: (path: string, layer: number, sf2Preset = -1) =>
    post<WithParams & { instrumentName: string; message?: string }>('/api/load', { path, layer, sf2Preset }, LOAD_TIMEOUT_MS),
  clearPianoLayer: (layer: number) => post<WithParams>('/api/piano/layers/clear', { layer }),
  /** Preset names in bank order; the array index is the sf2Preset value for loadSample(). */
  sf2Presets: (path: string) => get<{ presets: string[] }>(`/api/sf2/presets?path=${q(path)}`, 30000),
  pianoPerformances: () => get<{ performances: { name: string; layerCount?: number; legacy?: boolean }[] }>('/api/piano/performances'),
  storePianoPerformance: (name: string) => post('/api/piano/performances/store', { name }),
  loadPianoPerformance: (name: string) => post<WithParams>('/api/piano/performances/load', { name }, LOAD_TIMEOUT_MS),
  deletePianoPerformance: (name: string) => post('/api/piano/performances/delete', { name }),

  // ---- library -------------------------------------------------------------
  library: (dir?: string) => get<{ items: LibraryItem[]; rootDir: string }>(dir ? `/api/library?dir=${q(dir)}` : '/api/library', 60000),
  tagLibrary: (name: string, patch: { favorite?: boolean; group?: string }) => post('/api/library/tag', { name, ...patch }),
  sampleLibraryStatus: () => get<SampleLibraryStatus>('/api/sample-library/status'),
  sampleLibrarySource: (action: 'add' | 'remove', type: 'root' | 'file', path: string) =>
    post<SampleLibraryStatus>('/api/sample-library/source', { action, type, path }),
  sampleLibraryCacheDir: (path: string) => post('/api/sample-library/cache-directory', { path }),
  sampleLibraryPrepare: () => post('/api/sample-library/prepare'),
  sampleLibraryCancel: () => post('/api/sample-library/cancel'),
  sampleLibraryRelocate: (oldRoot: string, newRoot: string) =>
    post<{ message: string; updatedFiles?: number }>('/api/sample-library/relocate', { oldRoot, newRoot }, 60000),

  // ---- sounds (per-module snapshots referencing sample assets) -------------
  sounds: (module?: string) => get<{ sounds: SoundSummary[]; selected: Json }>(module ? `/api/sounds?module=${q(module)}` : '/api/sounds'),
  saveSound: (body: { module: string; name: string; category?: string; layer?: number; id?: string; variant?: boolean }) =>
    post<{ sound: SoundSummary }>('/api/sounds/save', body),
  loadSound: (id: string, layer?: number) => post<WithParams>('/api/sounds/load', layer === undefined ? { id } : { id, layer }, LOAD_TIMEOUT_MS),
  deleteSound: (id: string) => post('/api/sounds/delete', { id }),
  duplicateSound: (id: string, name: string) => post('/api/sounds/duplicate', { id, name }),
  setDefaultSound: (id: string) => post('/api/sounds/set-default', { id }),

  // ---- synth ---------------------------------------------------------------
  loadSynthFactory: (id: number) => post<WithParams>('/api/synth/factory/load', { id }),
  dx7Banks: () => get<{ ok: boolean; banks: Dx7Bank[] }>('/api/synth/dx7/banks'),
  dx7Load: (layer: number, bank: string, voice: number) => post<{ ok: boolean; name: string }>('/api/synth/dx7/load', { layer, bank, voice }),
  dx7Import: (name: string, hex: string) => post<{ ok: boolean; bank: Dx7Bank }>('/api/synth/dx7/import', { name, hex }),
  dx7Delete: (bank: string) => post<{ ok: boolean }>('/api/synth/dx7/delete', { bank }),
  mt32Status: () => get<Mt32Status>('/api/synth/mt32/status'),
  synthModSet: (connections: unknown[]) => post<{ ok: boolean }>('/api/synth/mod', { connections }),
  synthLayerApply: (layer: number, config: unknown | null) => post<{ ok: boolean }>('/api/synth/layer', { layer, config }),
  d50Banks: () => get<{ ok: boolean; banks: D50Bank[] }>('/api/synth/d50/banks'),
  d50Load: (bank: string, patch: number) => post<{ ok: boolean; name: string; mode: string }>('/api/synth/d50/load', { bank, patch }),
  mt32Load: (layer: number, program: number) => post<{ ok: boolean; name: string; romsLoaded: boolean }>('/api/synth/mt32/load', { layer, program }),
  synthFactoryMeta: () => get<FactoryMeta>('/api/synth/factory/edits'),
  synthFactoryFavorite: (id: number, favorite: boolean) =>
    post<{ ok: boolean; metadata: FactoryMeta }>('/api/synth/factory/meta', { op: 'favorite', id, favorite }),
  /** Saves the current synth edit buffer over factory slot `id` (stored as a user override). */
  synthFactorySave: (id: number, name: string) => post<{ ok: boolean; metadata: FactoryMeta }>('/api/synth/factory/save', { id, name }),
  /** Discards the saved edit of factory slot `id` (the bundled preset is used again). */
  synthFactoryReset: (id: number) => post<{ ok: boolean; metadata: FactoryMeta }>('/api/synth/factory/meta', { op: 'reset', id }),
  synthPresets: () => get<NamedPreset[]>('/api/synth/presets'),
  saveSynthPreset: (name: string) => post<{ presets: NamedPreset[] }>('/api/synth/presets/save', { name, mode: 'new' }),
  loadSynthPreset: (name: string) => post<WithParams>('/api/synth/presets/load', { name }),
  deleteSynthPreset: (name: string) => post<{ presets: NamedPreset[] }>('/api/synth/presets/delete', { name }),

  // ---- organ ---------------------------------------------------------------
  organFactory: () => get<{ factoryId: number; name: string }[]>('/api/organ/factory'),
  loadOrganFactory: (id: number) => post<WithParams>('/api/organ/factory/load', { id }),
  organPresets: () => get<NamedPreset[]>('/api/organ/presets'),
  saveOrganPreset: (name: string) => post<{ presets: NamedPreset[] }>('/api/organ/presets/save', { name }),
  loadOrganPreset: (name: string) => post<WithParams>('/api/organ/presets/load', { name }),
  deleteOrganPreset: (name: string) => post<{ presets: NamedPreset[] }>('/api/organ/presets/delete', { name }),

  // ---- studio racks --------------------------------------------------------
  studioRacks: () => get<{ racks: StudioRack[]; mode: boolean }>('/api/studio-racks'),
  studio: (body: Json) => post('/api/studio-racks', body),

  // ---- hardware & control mapping -----------------------------------------
  hardware: () => get<HardwareStatus>('/api/hardware'),
  rescanHardware: () => post<HardwareStatus>('/api/hardware/rescan', {}, 30000),
  configureAudio: (uid: number, sampleRate: number, bufferSize: number) =>
    post<HardwareStatus & { ok?: boolean; message?: string }>('/api/hardware/audio', { uid, sampleRate, bufferSize }, 30000),
  setMidiPort: (name: string, enabled: boolean) => post<HardwareStatus>('/api/hardware/midi', { name, enabled }),
  refreshMidi: () => post<HardwareStatus>('/api/hardware/midi', { refresh: true }),
  control: () => get<ControlState>('/api/control'),
  mappings: () => get<MappingState>('/api/mappings'),
  mappingsLearn: () => get<{ learn: LearnState; lastInput: Json | null }>('/api/mappings/learn'),
  mappingOp: (body: Json) => post<MappingState>('/api/mappings', body),
};
