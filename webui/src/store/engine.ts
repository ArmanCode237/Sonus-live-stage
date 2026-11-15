// Engine store. Components subscribe through hooks (useSyncExternalStore).
//
// Transport: the WebSocket /ws pushes a snapshot, then RFC 6902 patches of the
// edit buffer and ~30 Hz live-state frames; parameter writes travel on the same
// socket with acknowledgements. When the socket is down the store falls back to
// the original HTTP loop (GET /api/state polling + POST /api/param) and keeps
// trying to reconnect, so the UI never depends on one channel.
//
//  · Polling is sequential (never more than one request in flight), fast while
//    visible and slow when the tab is hidden.
//  · `params` keeps its identity while paramRev is unchanged, so parameter
//    controls only re-render when the edit buffer really changes; meters and
//    keys update every poll.
//  · Writes are coalesced per parameter (latest value wins, ≤ 1 request in
//    flight per id) and shown optimistically until the engine reflects them.
import { api } from '../api/client';
import { applyPatch } from '../api/jsonPatch';
import { RealtimeLink, type Frame } from '../api/realtime';
import type { EngineParams, EngineState, ParameterCatalogEntry } from '../api/types';
import { readParam } from '../params/paramMap';
import { makeSpec, type ParamSpec } from '../params/spec';

const POLL_VISIBLE_MS = 70;
const POLL_HIDDEN_MS = 600;
const POLL_RETRY_MS = 1200;
const SEND_INTERVAL_MS = 35;
/** Over the socket a write per animation frame is cheap. */
const SEND_INTERVAL_WS_MS = 16;
/** Keep the optimistic value this long after the last write is acknowledged. */
const OVERRIDE_HOLD_MS = 900;

export type Connection = 'connecting' | 'online' | 'offline';
export type Transport = 'ws' | 'http';

interface Override { value: number; acknowledgedAt: number | null }

type Listener = () => void;

class EngineStore {
  state: EngineState | null = null;
  connection: Connection = 'connecting';
  error = '';
  private listeners = new Set<Listener>();
  private overrides = new Map<string, Override>();
  private pending = new Map<string, number>();
  private inFlight = new Set<string>();
  private lastSent = new Map<string, number>();
  private timers = new Map<string, ReturnType<typeof setTimeout>>();
  private specs = new Map<string, ParamSpec>();
  private readonly modules = new Map<string, number>();
  private lastParamRev = -1;
  private pollTimer: ReturnType<typeof setTimeout> | null = null;
  private started = false;
  // All changes to a preset must form one edit-buffer transaction. Otherwise
  // a throttled knob POST from preset A can arrive after preset B is loaded.
  private writesSuspended = false;
  // Invalidate GET /api/state snapshots started before a preset transaction.
  private stateEpoch = 0;
  private activeWrites = new Set<Promise<unknown>>();
  private link: RealtimeLink | null = null;
  private patchSeq = -1;
  transport: Transport = 'http';
  version = 0;

  // ---------------------------------------------------------------- lifecycle
  start(): void {
    if (this.started) return;
    this.started = true;
    void this.loadCatalog();
    this.link = new RealtimeLink({
      onOpen: () => { this.transport = 'ws'; this.stopPolling(); this.emit(); },
      onClose: () => { this.transport = 'http'; this.patchSeq = -1; this.refresh(); },
      onFrame: frame => this.onFrame(frame),
    });
    this.link.connect();
    this.poll(); // HTTP until the socket is up (and whenever it drops)
    document.addEventListener('visibilitychange', () => { if (!document.hidden) this.refresh(); });
  }

  subscribe = (listener: Listener): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };
  private emit(): void {
    this.version++;
    this.listeners.forEach(l => l());
  }

  /** Poll immediately (after an action that changed the engine). The socket
   *  already streams every change, so this only matters on the HTTP fallback. */
  refresh(): void {
    if (this.link?.isOpen) return;
    this.stopPolling();
    this.poll();
  }

  private stopPolling(): void {
    if (this.pollTimer) clearTimeout(this.pollTimer);
    this.pollTimer = null;
  }

  // ---------------------------------------------------------------- realtime
  private onFrame(frame: Exclude<Frame, { t: 'ack' }>): void {
    if (frame.t === 'snapshot') {
      this.patchSeq = frame.seq;
      this.state = { ...frame.state, params: frame.params } as EngineState;
    } else if (frame.t === 'patch') {
      if (!this.state || frame.seq !== this.patchSeq + 1) { this.link?.send({ op: 'resync' }); return; }
      try {
        this.state = { ...this.state, params: applyPatch(this.state.params, frame.ops) };
        this.patchSeq = frame.seq;
      } catch { this.link?.send({ op: 'resync' }); return; }
    } else if (frame.t === 'state') {
      if (!this.state) return;  // wait for the snapshot
      const { t: _t, seq: _seq, ...live } = frame;
      this.state = { ...(live as Omit<EngineState, 'params'>), params: this.state.params };
    }
    this.pruneOverrides();
    if (this.connection !== 'online') { this.connection = 'online'; this.error = ''; }
    this.emit();
  }

  private polling = false;
  private async poll(): Promise<void> {
    if (this.polling || this.link?.isOpen) return;
    this.polling = true;
    let delay = POLL_VISIBLE_MS;
    const epoch = this.stateEpoch;
    try {
      const next = await api.state();
      if (epoch === this.stateEpoch && !this.writesSuspended) {
        if (this.state && next.paramRev === this.lastParamRev) next.params = this.state.params;
        else this.lastParamRev = next.paramRev;
        this.state = next;
        this.pruneOverrides();
      }
      if (this.connection !== 'online') { this.connection = 'online'; this.error = ''; }
      delay = document.hidden ? POLL_HIDDEN_MS : POLL_VISIBLE_MS;
    } catch (e) {
      this.connection = 'offline';
      this.error = (e as Error).message;
      delay = POLL_RETRY_MS;
    } finally {
      this.polling = false;
    }
    this.emit();
    if (!this.link?.isOpen) this.pollTimer = setTimeout(() => this.poll(), delay);
  }

  /** Apply a full params snapshot returned by an action (load, recall…). */
  applyParams(params: EngineParams | undefined): void {
    if (!params || !this.state) { this.refresh(); return; }
    this.state = { ...this.state, params };
    this.overrides.clear();
    this.emit();
    this.refresh();
  }

  // ---------------------------------------------------------------- catalog
  private async loadCatalog(): Promise<void> {
    for (let attempt = 0; attempt < 30; attempt++) {
      try {
        const control = await api.control();
        this.setCatalog(control.parameters);
        return;
      } catch { await new Promise(r => setTimeout(r, 1500)); }
    }
  }
  setCatalog(entries: ParameterCatalogEntry[]): void {
    for (const e of entries) {
      this.specs.set(e.id, makeSpec(e.id, e.min, e.max));
      this.modules.set(e.id, e.module ?? -1);
    }
    this.emit();
  }
  /** Owning module of a parameter: 0 piano, 1 synth, 2 organ, -1 global (from the engine catalogue). */
  moduleOf(id: string): number { return this.modules.get(id) ?? -1; }
  spec(id: string): ParamSpec | undefined { return this.specs.get(id); }
  hasCatalog(): boolean { return this.specs.size > 0; }

  // ---------------------------------------------------------------- values
  value(id: string): number | undefined {
    const o = this.overrides.get(id);
    if (o) return o.value;
    return readParam(this.state?.params, id);
  }

  private pruneOverrides(): void {
    const now = Date.now();
    for (const [id, o] of this.overrides) {
      if (this.pending.has(id) || this.inFlight.has(id) || o.acknowledgedAt === null) continue;
      const engine = readParam(this.state?.params, id);
      const settled = engine !== undefined && Math.abs(engine - o.value) <= Math.max(1e-4, Math.abs(o.value) * 1e-4);
      if (settled || now - o.acknowledgedAt > OVERRIDE_HOLD_MS) this.overrides.delete(id);
    }
  }

  /**
   * Freeze edits while a Sound/Performance is replaced or saved. A replacement
   * discards unsent controls; a save flushes them first. Already-sent writes
   * always finish BEFORE the server receives a replacement action.
   */
  async beginEditTransaction(replace: boolean): Promise<() => void> {
    ++this.stateEpoch;
    this.writesSuspended = true;
    for (const timer of this.timers.values()) clearTimeout(timer);
    this.timers.clear();
    if (replace) {
      this.pending.clear();
      this.overrides.clear();
      this.emit();
    }
    // Keep draining the final knob position when the user presses STORE.
    while (this.pending.size || this.inFlight.size || this.activeWrites.size) {
      if (!replace) {
        for (const id of Array.from(this.pending.keys())) {
          if (!this.inFlight.has(id)) void this.flush(id);
        }
      }
      const active = Array.from(this.activeWrites);
      if (active.length) await Promise.allSettled(active);
      else await Promise.resolve();
    }
    return () => { ++this.stateEpoch; this.writesSuspended = false; };
  }

  /** Virtual keyboard note: over the socket when available, else REST. */
  note(key: number, on: boolean, vel = 100): void {
    if (this.link?.isOpen) this.link.send({ op: 'note', key, on, vel });
    else void api.note(key, on, vel).catch(() => {});
  }

  /** Set a parameter: optimistic locally, coalesced towards the engine. */
  set(id: string, value: number): void {
    if (this.writesSuspended) return;
    const spec = this.specs.get(id);
    const v = spec ? Math.min(spec.max, Math.max(spec.min, value)) : value;
    this.overrides.set(id, { value: v, acknowledgedAt: null });
    this.pending.set(id, v);
    this.emit();
    this.schedule(id);
  }

  private schedule(id: string): void {
    if (this.inFlight.has(id) || this.timers.has(id)) return;
    const interval = this.link?.isOpen ? SEND_INTERVAL_WS_MS : SEND_INTERVAL_MS;
    const wait = Math.max(0, interval - (Date.now() - (this.lastSent.get(id) ?? 0)));
    this.timers.set(id, setTimeout(() => { this.timers.delete(id); void this.flush(id); }, wait));
  }

  private async flush(id: string): Promise<void> {
    if (!this.pending.has(id)) return;
    const value = this.pending.get(id)!;
    this.pending.delete(id);
    this.inFlight.add(id);
    this.lastSent.set(id, Date.now());
    let request: Promise<unknown> | null = null;
    try {
      if (this.link?.isOpen) {
        // The acknowledged value arrives back as a patch on the same socket.
        request = this.link.request({ op: 'set', id, v: value }).then(ok => {
          if (!ok) throw new Error(`El motor rechazó ${id}`);
        });
        this.activeWrites.add(request);
        await request;
      } else {
        const rest = api.setParam(id, value);
        request = rest;
        this.activeWrites.add(rest);
        const response = await rest;
        if (response.params && this.state) this.state = { ...this.state, params: response.params };
      }
    } catch (e) {
      this.error = (e as Error).message;
    } finally {
      if (request) this.activeWrites.delete(request);
      this.inFlight.delete(id);
      const o = this.overrides.get(id);
      if (o && !this.pending.has(id)) o.acknowledgedAt = Date.now();
      if (this.pending.has(id)) this.schedule(id);
      this.emit();
    }
  }
}

export const engine = new EngineStore();
