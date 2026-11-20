import { describe, expect, it } from 'vitest';
import { applyPatch } from './jsonPatch';

describe('applyPatch', () => {
  const doc = { a: { b: 1, c: [1, 2, 3] }, d: { e: true }, 'x/y': 0 };
  it('replaces nested values and keeps untouched branches', () => {
    const out = applyPatch(doc, [{ op: 'replace', path: '/a/c/1', value: 9 }]);
    expect(out.a.c).toEqual([1, 9, 3]);
    expect(out.d).toBe(doc.d);            // structural sharing
    expect(doc.a.c[1]).toBe(2);           // input not mutated
  });
  it('adds and removes keys and array items', () => {
    const out = applyPatch(doc, [
      { op: 'add', path: '/a/n', value: 5 }, { op: 'remove', path: '/d/e' },
      { op: 'add', path: '/a/c/-', value: 4 }, { op: 'remove', path: '/a/c/0' },
    ]);
    expect(out.a).toEqual({ b: 1, c: [2, 3, 4], n: 5 });
    expect(out.d).toEqual({});
  });
  it('decodes escaped pointer tokens', () => {
    expect(applyPatch(doc, [{ op: 'replace', path: '/x~1y', value: 7 }])['x/y']).toBe(7);
  });
  it('rejects invalid paths', () => {
    expect(() => applyPatch(doc, [{ op: 'replace', path: '/a/b/z', value: 1 }])).toThrow();
  });
});
