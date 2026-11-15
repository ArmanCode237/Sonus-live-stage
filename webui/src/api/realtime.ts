// WebSocket link to the engine (/ws). Pushes snapshot / patch / state frames to
// the store and carries parameter writes with acknowledgements. Reconnects with
// backoff; while closed the store falls back to HTTP polling + REST writes.
import type { EngineParams, EngineState } from './types';
import type { PatchOp } from './jsonPatch';

export type LiveState = Omit<EngineState, 'params'>;
export type Frame =
  | { t: 'snapshot'; seq: number; state: LiveState; params: EngineParams }
  | { t: 'patch'; seq: number; ops: PatchOp[] }
  | ({ t: 'state'; seq: number } & LiveState)
  | { t: 'ack'; seq: number; ok: boolean };

export interface RealtimeHandlers {
  onFrame: (frame: Exclude<Frame, { t: 'ack' }>) => void;
  onOpen: () => void;
  onClose: () => void;
}

const ACK_TIMEOUT_MS = 3000;
const MAX_BACKOFF_MS = 5000;

export class RealtimeLink {
  private ws: WebSocket | null = null;
  private seq = 0;
  private backoff = 300;
  private stopped = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private pending = new Map<number, { resolve: (ok: boolean) => void; timer: ReturnType<typeof setTimeout> }>();

  constructor(private readonly handlers: RealtimeHandlers, private readonly url = defaultUrl()) {}

  get isOpen(): boolean { return this.ws?.readyState === WebSocket.OPEN; }

  connect(): void {
    if (typeof WebSocket === 'undefined' || this.stopped) return;
    let ws: WebSocket;
    try { ws = new WebSocket(this.url); } catch { this.scheduleReconnect(); return; }
    this.ws = ws;
    ws.onopen = () => { this.backoff = 300; this.handlers.onOpen(); };
    ws.onmessage = e => {
      let frame: Frame;
      try { frame = JSON.parse(String(e.data)) as Frame; } catch { return; }
      if (frame.t === 'ack') { this.settle(frame.seq, frame.ok); return; }
      this.handlers.onFrame(frame);
    };
    ws.onclose = () => {
      if (this.ws === ws) this.ws = null;
      for (const seq of Array.from(this.pending.keys())) this.settle(seq, false);
      this.handlers.onClose();
      this.scheduleReconnect();
    };
    ws.onerror = () => ws.close();
  }

  stop(): void {
    this.stopped = true;
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.ws?.close();
  }

  /** Sends a command; resolves with the engine's ok flag (false on timeout or disconnect). */
  request(message: Record<string, unknown>): Promise<boolean> {
    if (!this.isOpen) return Promise.resolve(false);
    const seq = ++this.seq;
    return new Promise(resolve => {
      const timer = setTimeout(() => this.settle(seq, false), ACK_TIMEOUT_MS);
      this.pending.set(seq, { resolve, timer });
      this.ws!.send(JSON.stringify({ ...message, seq }));
    });
  }

  /** Fire-and-forget message (resync, notes). */
  send(message: Record<string, unknown>): void {
    if (this.isOpen) this.ws!.send(JSON.stringify(message));
  }

  private settle(seq: number, ok: boolean): void {
    const p = this.pending.get(seq);
    if (!p) return;
    clearTimeout(p.timer);
    this.pending.delete(seq);
    p.resolve(ok);
  }

  private scheduleReconnect(): void {
    if (this.stopped || this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => { this.reconnectTimer = null; this.connect(); }, this.backoff);
    this.backoff = Math.min(MAX_BACKOFF_MS, this.backoff * 2);
  }
}

function defaultUrl(): string {
  if (typeof location === 'undefined') return 'ws://localhost:8080/ws';
  return `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`;
}
