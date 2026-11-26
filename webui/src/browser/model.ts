// Sound browser model: one item shape for piano, synth and organ, plus search,
// filters (facets), sorting and grouping. Pure functions (unit-tested).

export type BrowserModule = 'piano' | 'synth' | 'organ';
export type Origin = 'factory' | 'edited' | 'user' | 'dx7' | 'd50' | 'mt32' | 'sample' | 'sound' | 'performance';

export const ORIGIN_LABEL: Record<Origin, string> = {
  factory: 'FÁBRICA', edited: 'EDITADO', user: 'USUARIO', dx7: 'DX7', d50: 'D-50', mt32: 'MT-32',
  // Piano: every file is a Sound — with the file's own settings, or with settings saved by the user.
  sample: 'DEL ARCHIVO', sound: 'GUARDADO', performance: 'PERFORMANCE',
};

/** Context of a load: the synth layer that receives a DX7 / MT-32 voice, the piano layer… */
export interface LoadContext { layer: number }

/** A sub-choice inside one item (the presets of an SF2 bank). */
export interface SubItem { label: string; load: (ctx: LoadContext) => Promise<boolean> }

export interface BrowserItem {
  key: string;
  module: BrowserModule;
  origin: Origin;
  name: string;
  number?: string;
  /** Category from the source (factory category, library group…), before user overrides. */
  baseCategory: string;
  /** Tags coming from the source itself (piano Sounds); user tags are merged on top. */
  baseTags?: string[];
  /** Synth engine(s) or sample format. */
  engine?: string;
  /** Short text on the right of a row (bank, format · size…). */
  detail?: string;
  /** Rows of the details panel. */
  info?: [string, string][];
  /** Favourite kept by the source (synth factory, sample library); others live in the browser metadata. */
  sourceFavorite?: boolean;
  toggleSourceFavorite?: () => Promise<void>;
  /** The item needs a target layer (DX7 / MT-32 voices, piano instruments). */
  needsLayer?: boolean;
  /** Extra explanation of what loading replaces (D-50: how its tones map onto the layers). */
  scopeNote?: string;
  load: (ctx: LoadContext) => Promise<boolean>;
  subItems?: () => Promise<SubItem[] | null>;
  rename?: (to: string) => Promise<boolean>;
  duplicate?: (to: string) => Promise<boolean>;
  remove?: () => Promise<boolean>;
  restore?: () => Promise<boolean>;
  restoreLabel?: string;
}

/** An item with its user metadata applied (what the UI shows). */
export interface ResolvedItem extends BrowserItem {
  category: string;
  tags: string[];
  favorite: boolean;
  uses: number;
  lastUsed: number;
}

/** What a load replaces: the whole module (patch, performance) or one layer (voice, instrument). */
export type Scope = 'module' | 'layer';
export const scopeOf = (item: Pick<BrowserItem, 'needsLayer'>): Scope => (item.needsLayer ? 'layer' : 'module');

export interface Filters {
  query: string;
  scope: Scope | null;
  origin: Origin | 'all';
  favorites: boolean;
  recents: boolean;
  category: string | null;
  tags: string[];
  engine: string | null;
}
export const EMPTY_FILTERS: Filters = { query: '', scope: null, origin: 'all', favorites: false, recents: false, category: null, tags: [], engine: null };

export type SortKey = 'number' | 'name' | 'category' | 'recent' | 'used' | 'origin';
export const SORT_LABEL: Record<SortKey, string> = {
  number: 'NÚMERO', name: 'NOMBRE', category: 'CATEGORÍA', recent: 'RECIENTES', used: 'MÁS USADOS', origin: 'ORIGEN',
};

/** Lower case without accents: "Órgano" matches "organo". */
export const normalise = (s: string) => s.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase();

/** Search: every word must appear in name / category / tags / origin / engine / detail / number.
 *  "#tag" requires that exact tag. */
export function matchesQuery(item: ResolvedItem, query: string): boolean {
  const words = normalise(query).split(/\s+/).filter(Boolean);
  if (!words.length) return true;
  const hay = normalise([item.name, item.category, ORIGIN_LABEL[item.origin], item.engine ?? '', item.detail ?? '', item.number ?? '',
    ...item.tags.map(t => `#${t}`)].join(' '));
  return words.every(w => (w.startsWith('#') && w.length > 1 ? item.tags.some(t => normalise(t) === w.slice(1)) : hay.includes(w)));
}

export const RECENT_LIMIT = 30;

/** Items that pass every filter except the one named in `skip` (facet counts ignore their own filter). */
export function applyFilters(items: ResolvedItem[], f: Filters, skip?: keyof Filters): ResolvedItem[] {
  let recentKeys: Set<string> | null = null;
  if (f.recents && skip !== 'recents') {
    recentKeys = new Set(items.filter(i => i.lastUsed > 0).sort((a, b) => b.lastUsed - a.lastUsed).slice(0, RECENT_LIMIT).map(i => i.key));
  }
  return items.filter(i =>
    (skip === 'origin' || f.origin === 'all' || i.origin === f.origin) &&
    (skip === 'scope' || !f.scope || scopeOf(i) === f.scope) &&
    (skip === 'favorites' || !f.favorites || i.favorite) &&
    (!recentKeys || recentKeys.has(i.key)) &&
    (skip === 'category' || !f.category || i.category === f.category) &&
    (skip === 'engine' || !f.engine || i.engine === f.engine) &&
    (skip === 'tags' || f.tags.every(t => i.tags.includes(t))) &&
    (skip === 'query' || matchesQuery(i, f.query)));
}

const collator = new Intl.Collator('es', { numeric: true, sensitivity: 'base' });
/** Origins numbered inside banks (cartridges): the bank comes before the number. */
const BANKED = new Set<Origin>(['dx7', 'd50']);
const ORIGIN_ORDER: Origin[] = ['factory', 'edited', 'user', 'sound', 'performance', 'sample', 'dx7', 'd50', 'mt32'];

export function sortItems(items: ResolvedItem[], key: SortKey): ResolvedItem[] {
  const byName = (a: ResolvedItem, b: ResolvedItem) => collator.compare(a.name, b.name);
  const byNumber = (a: ResolvedItem, b: ResolvedItem) =>
    ORIGIN_ORDER.indexOf(a.origin) - ORIGIN_ORDER.indexOf(b.origin) ||
    (BANKED.has(a.origin) ? collator.compare(a.detail ?? '', b.detail ?? '') : 0) ||
    collator.compare(a.number ?? '', b.number ?? '') || byName(a, b);
  const list = [...items];
  switch (key) {
    case 'name': return list.sort(byName);
    case 'category': return list.sort((a, b) => collator.compare(a.category, b.category) || byNumber(a, b));
    case 'recent': return list.sort((a, b) => b.lastUsed - a.lastUsed || byNumber(a, b));
    case 'used': return list.sort((a, b) => b.uses - a.uses || b.lastUsed - a.lastUsed || byNumber(a, b));
    case 'origin': return list.sort((a, b) => ORIGIN_ORDER.indexOf(a.origin) - ORIGIN_ORDER.indexOf(b.origin) || byName(a, b));
    default: return list.sort(byNumber);
  }
}

/** Value -> count, sorted by value (categories, engines) or by count (tags). */
export function facetCounts(items: ResolvedItem[], pick: (i: ResolvedItem) => string | string[] | undefined, byCount = false): [string, number][] {
  const m = new Map<string, number>();
  for (const i of items) {
    const v = pick(i);
    for (const x of Array.isArray(v) ? v : v ? [v] : []) m.set(x, (m.get(x) ?? 0) + 1);
  }
  return [...m.entries()].sort((a, b) => (byCount ? b[1] - a[1] : 0) || collator.compare(a[0], b[0]));
}

/** Rows of the list: items, or category headers followed by their items. */
export type Row = { kind: 'header'; label: string; count: number } | { kind: 'item'; item: ResolvedItem };
export function groupRows(items: ResolvedItem[], grouped: boolean): Row[] {
  if (!grouped) return items.map(item => ({ kind: 'item', item }));
  const groups = new Map<string, ResolvedItem[]>();
  for (const i of items) { const g = groups.get(i.category); if (g) g.push(i); else groups.set(i.category, [i]); }
  const rows: Row[] = [];
  for (const [label, list] of groups) { rows.push({ kind: 'header', label, count: list.length }); for (const item of list) rows.push({ kind: 'item', item }); }
  return rows;
}
