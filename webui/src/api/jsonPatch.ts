// Minimal immutable RFC 6902 applier for the ops nlohmann::json::diff emits
// (add / remove / replace). Only the objects along each changed path are
// copied, so untouched branches keep their identity (cheap React updates).
export interface PatchOp { op: 'add' | 'remove' | 'replace' | string; path: string; value?: unknown }

type Container = Record<string, unknown> | unknown[];

const decode = (token: string) => token.replace(/~1/g, '/').replace(/~0/g, '~');

function setIn(node: unknown, keys: string[], op: PatchOp): unknown {
  if (keys.length === 0) return op.op === 'remove' ? undefined : op.value;
  const [key, ...rest] = keys;
  if (Array.isArray(node)) {
    const copy = node.slice();
    const index = key === '-' ? copy.length : Number(key);
    if (!Number.isInteger(index) || index < 0 || index > copy.length) throw new Error(`bad index ${key}`);
    if (rest.length === 0) {
      if (op.op === 'remove') copy.splice(index, 1);
      else if (op.op === 'add') copy.splice(index, 0, op.value);
      else copy[index] = op.value;
    } else copy[index] = setIn(copy[index], rest, op);
    return copy;
  }
  if (node === null || typeof node !== 'object') throw new Error(`path through non-container at ${key}`);
  const copy: Record<string, unknown> = { ...(node as Record<string, unknown>) };
  if (rest.length === 0 && op.op === 'remove') delete copy[key];
  else copy[key] = setIn(copy[key], rest, op);
  return copy;
}

/** Returns a new document; throws on an unsupported op or an invalid path. */
export function applyPatch<T extends Container>(doc: T, ops: readonly PatchOp[]): T {
  let out: unknown = doc;
  for (const op of ops) {
    if (op.op !== 'add' && op.op !== 'remove' && op.op !== 'replace') throw new Error(`unsupported op ${op.op}`);
    const keys = op.path === '' ? [] : op.path.slice(1).split('/').map(decode);
    out = setIn(out, keys, op);
  }
  return out as T;
}
