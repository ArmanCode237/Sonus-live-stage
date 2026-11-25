import { describe, expect, it } from 'vitest';
import { SYNTH_FACTORY } from '../data/synthFactory';
import { SYNTH_FACTORY_ENGINES } from '../data/synthFactoryEngines';
import { applyFilters, EMPTY_FILTERS, facetCounts, groupRows, matchesQuery, normalise, sortItems, type ResolvedItem } from './model';

const item = (over: Partial<ResolvedItem>): ResolvedItem => ({
  key: over.name ?? 'x', module: 'synth', origin: 'factory', name: 'X', baseCategory: 'PAD', category: 'PAD',
  tags: [], favorite: false, uses: 0, lastUsed: 0, load: async () => true, ...over,
});

const items = [
  item({ key: 'a', name: 'Órgano Cálido', number: '003', category: 'ORGAN', tags: ['warm'], engine: 'VA' }),
  item({ key: 'b', name: 'Brass Stab', number: '001', category: 'BRASS', favorite: true, engine: 'FM', uses: 5, lastUsed: 300 }),
  item({ key: 'c', name: 'My Pad', origin: 'user', category: 'PAD', tags: ['warm', 'live'], engine: 'VA', uses: 2, lastUsed: 500 }),
  item({ key: 'd', name: 'E.Piano 1', origin: 'dx7', number: '01', category: 'DX7', engine: 'DX7', detail: 'ROM1A' }),
];

describe('browser model', () => {
  it('normalises accents and case', () => {
    expect(normalise('Órgano CÁLIDO')).toBe('organo calido');
  });

  it('searches every word across name, category, tags and origin', () => {
    expect(matchesQuery(items[0], 'organo calido')).toBe(true);
    expect(matchesQuery(items[0], 'organo brass')).toBe(false);
    expect(matchesQuery(items[2], 'usuario pad')).toBe(true);
    expect(matchesQuery(items[3], 'rom1a')).toBe(true);
  });

  it('#tag requires an exact tag', () => {
    expect(matchesQuery(items[2], '#live')).toBe(true);
    expect(matchesQuery(items[2], '#liv')).toBe(false);
  });

  it('combines filters and lets a facet ignore its own filter', () => {
    const f = { ...EMPTY_FILTERS, tags: ['warm'], engine: 'VA' };
    expect(applyFilters(items, f).map(i => i.key)).toEqual(['a', 'c']);
    expect(applyFilters(items, { ...f, origin: 'user' }).map(i => i.key)).toEqual(['c']);
    expect(applyFilters(items, { ...f, origin: 'user' }, 'origin').map(i => i.key)).toEqual(['a', 'c']);
    expect(applyFilters(items, { ...EMPTY_FILTERS, favorites: true }).map(i => i.key)).toEqual(['b']);
    expect(applyFilters(items, { ...EMPTY_FILTERS, recents: true }).map(i => i.key).sort()).toEqual(['b', 'c']);
  });

  it('sorts by number (origin first), name, use and recency', () => {
    expect(sortItems(items, 'number').map(i => i.key)).toEqual(['b', 'a', 'c', 'd']);
    const bank = [item({ key: 'x', number: '049', detail: 'BELLS' }), item({ key: 'y', number: '008', detail: 'BASS' }), item({ key: 'z', number: '002', detail: 'PADS' })];
    expect(sortItems(bank, 'number').map(i => i.key)).toEqual(['z', 'y', 'x']);
    expect(sortItems(items, 'name').map(i => i.key)).toEqual(['b', 'd', 'c', 'a']);
    expect(sortItems(items, 'used').map(i => i.key).slice(0, 2)).toEqual(['b', 'c']);
    expect(sortItems(items, 'recent')[0].key).toBe('c');
  });

  it('counts facets and groups rows by category', () => {
    expect(facetCounts(items, i => i.tags, true)).toEqual([['warm', 2], ['live', 1]]);
    const rows = groupRows(sortItems(items, 'category'), true);
    expect(rows.filter(r => r.kind === 'header').map(r => r.kind === 'header' && r.label)).toEqual(['BRASS', 'DX7', 'ORGAN', 'PAD']);
    expect(groupRows(items, false)).toHaveLength(items.length);
  });

  it('has engine data for every factory synth preset', () => {
    const missing = SYNTH_FACTORY.filter(p => !SYNTH_FACTORY_ENGINES[p.id]?.length).map(p => p.id);
    expect(missing).toEqual([]);
  });
});
