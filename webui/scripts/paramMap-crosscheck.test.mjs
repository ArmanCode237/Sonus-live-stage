// Writes what paramMap.ts resolves for every catalogue parameter to
// src/params/__fixtures__/resolved.json. tests/v31_param_reader.cpp requires the C++
// reader (src/control/ParamValueReader.cpp) to return exactly the same values, so the
// two resolvers cannot drift apart. Runs with `npm test`.
import { readFileSync, writeFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { readParam } from '../src/params/paramMap.ts';

const fixture = name => new URL(`../src/params/__fixtures__/${name}`, import.meta.url);

describe('paramMap cross-check fixture', () => {
  it('records the resolved value of every parameter', () => {
    const params = JSON.parse(readFileSync(fixture('params.json'), 'utf8'));
    const catalog = JSON.parse(readFileSync(fixture('catalog.json'), 'utf8'));
    const resolved = {};
    for (const { id } of catalog) resolved[id] = readParam(params, id) ?? null;
    writeFileSync(fixture('resolved.json'), JSON.stringify(resolved, null, 1));
    expect(Object.keys(resolved).length).toBe(catalog.length);
  });
});
