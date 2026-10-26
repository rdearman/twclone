import { firstValueFrom, Subscription } from 'rxjs';
import { ClientEventTransport, EventResumeTransport, EventsResumeRequest, GameEventEnvelope } from './transport.service';
import { StateService, ConnectionState } from './state.service';

export interface EventCursorStore {
  load(playerKey: string): string | null;
  save(playerKey: string, cursor: string): void;
  clear(playerKey: string): void;
}

export type ResumeHistoryResult = 'complete' | 'invalid_page' | 'resync_required' | 'unavailable';

/** Browser cursor storage is player-scoped because cursors belong to one event stream. */
export class BrowserEventCursorStore implements EventCursorStore {
  private storage(): Storage | null {
    try {
      return typeof globalThis.localStorage === 'undefined' ? null : globalThis.localStorage;
    } catch {
      return null;
    }
  }

  load(playerKey: string): string | null {
    try {
      return this.storage()?.getItem(this.key(playerKey)) ?? null;
    } catch {
      return null;
    }
  }

  save(playerKey: string, cursor: string): void {
    try {
      this.storage()?.setItem(this.key(playerKey), cursor);
    } catch {
      // Storage may be disabled or full; the in-memory state still tracks the cursor.
    }
  }

  clear(playerKey: string): void {
    try {
      this.storage()?.removeItem(this.key(playerKey));
    } catch {
      // Clearing best-effort storage must not block logout.
    }
  }

  private key(playerKey: string): string {
    return `twclone:event-cursor:${encodeURIComponent(playerKey)}`;
  }
}

/** Validate the documented envelope without interpreting its opaque cursor. */
export function parseGameEventEnvelope(value: unknown): GameEventEnvelope | null {
  if (!isRecord(value)) return null;
  const { id, ts, type, data, meta } = value;
  if (typeof id !== 'string' || !isUuid(id)) return null;
  if (typeof ts !== 'string' || !Number.isFinite(Date.parse(ts))) return null;
  if (typeof type !== 'string' || !type.trim()) return null;
  if (!isRecord(data) || !isRecord(meta)) return null;
  if (typeof meta['topic'] !== 'string' || !meta['topic'].trim()) return null;
  if (typeof meta['replayable'] !== 'boolean') return null;

  if (meta['replayable']) {
    if (typeof meta['cursor'] !== 'string' || !meta['cursor'].length) return null;
  } else if ('cursor' in meta) {
    return null;
  }

  return value as unknown as GameEventEnvelope;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

function isUuid(value: string): boolean {
  return /^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(value);
}

/**
 * Client-side event state coordinator. It consumes an injected transport so
 * browser and Capacitor builds share the same reconnect and state handling.
 */
export class EventBridgeCoordinator {
  private playerKey: string | null = null;
  private hasConnected = false;
  private reconnectPending = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private reconnectDelayMs = 1000;
  private refreshInProgress = false;
  private paused = false;
  private readonly subscriptions: Subscription;

  constructor(
    private readonly state: StateService,
    private readonly stream: ClientEventTransport,
    private readonly resumeTransport: EventResumeTransport | null,
    private readonly cursors: EventCursorStore,
    private readonly refreshAuthoritativeState?: () => Promise<void>
  ) {
    this.subscriptions = new Subscription();
    this.subscriptions.add(stream.events$.subscribe(raw => this.consume(raw)));
    this.subscriptions.add(stream.connectionState$.subscribe(value => this.onConnectionState(value)));
    this.subscriptions.add(stream.resyncRequired$.subscribe(() => void this.handleResyncRequired()));
  }

  connect(playerId: string | number): void {
    const playerKey = String(playerId).trim();
    if (!playerKey) throw new Error('A player ID is required to resume its event stream.');
    if (this.playerKey && this.playerKey !== playerKey) this.stream.disconnect();
    this.playerKey = playerKey;
    this.paused = false;
    const cursor = this.cursors.load(playerKey);
    if (this.hasConnected) {
      this.state.markReconnectAttempt();
      this.reconnectPending = true;
    }
    else this.state.markConnecting(false);
    this.hasConnected = true;
    this.stream.connect(cursor);
  }

  /** Retry the active player's stream after a transport failure. */
  reconnect(): void {
    if (this.playerKey) this.connect(this.playerKey);
  }

  pause(): void {
    if (!this.playerKey) return;
    this.paused = true;
    this.clearReconnectTimer();
    this.stream.disconnect();
    this.state.markDisconnected();
  }

  resume(): void {
    this.paused = false;
    this.reconnect();
  }

  /** Call after the gateway has registered the authenticated live stream. */
  async resumeHistory(limit = 100): Promise<ResumeHistoryResult> {
    if (!this.playerKey || !this.resumeTransport || !Number.isInteger(limit) || limit < 1 || limit > 500) return 'unavailable';
    let afterCursor = this.cursors.load(this.playerKey) ?? undefined;
    let throughCursor: string | undefined;
    let firstPage = true;

    try {
      while (true) {
        const request: EventsResumeRequest = { limit };
        if (afterCursor !== undefined) request.after_cursor = afterCursor;
        if (!firstPage && throughCursor !== undefined) request.through_cursor = throughCursor;
        const page = await firstValueFrom(this.resumeTransport.resumeEvents(request));
        if (!isResumePage(page)) return 'invalid_page';
        if (firstPage) throughCursor = page.high_watermark;
        for (const event of page.events) {
          if (!parseGameEventEnvelope(event)) return 'invalid_page';
          this.consume(event);
        }
        if (!page.has_more) return 'complete';
        if (page.next_cursor === afterCursor) return 'invalid_page';
        afterCursor = page.next_cursor;
        firstPage = false;
      }
    } catch (error) {
      if (isResyncRequired(error)) {
        this.cursors.clear(this.playerKey);
        this.state.markAuthoritativeStateStale();
        await this.refreshAuthoritativeState?.();
        return 'resync_required';
      }
      return 'unavailable';
    }
  }

  disconnect(clearCursor = false): void {
    const playerKey = this.playerKey;
    this.playerKey = null;
    this.paused = false;
    this.clearReconnectTimer();
    this.stream.disconnect();
    if (playerKey && clearCursor) this.cursors.clear(playerKey);
    this.state.markDisconnected();
  }

  destroy(): void {
    this.clearReconnectTimer();
    this.subscriptions.unsubscribe();
    this.stream.disconnect();
  }

  private consume(raw: unknown): boolean {
    const event = parseGameEventEnvelope(raw);
    if (!event || !this.playerKey) return false;
    const cursor = event.meta.replayable ? event.meta.cursor : undefined;
    const accepted = this.state.recordEvent({
      id: event.id,
      type: event.type,
      data: event.data,
      cursor
    });
    if (accepted && cursor) this.cursors.save(this.playerKey, cursor);
    // A valid duplicate is already applied and should not abort a resume page.
    return true;
  }

  private onConnectionState(value: ConnectionState): void {
    if (value === 'connecting') this.state.markConnecting(false);
    else if (value === 'reconnecting') {
      if (!this.reconnectPending) this.state.markReconnectAttempt();
      this.reconnectPending = true;
      this.scheduleReconnect();
    } else if (value === 'online') {
      this.reconnectDelayMs = 1000;
      this.state.markStreamConnected();
      this.reconnectPending = false;
    } else {
      this.state.markDisconnected();
      this.reconnectPending = false;
      if (this.playerKey && !this.refreshInProgress && !this.paused) this.scheduleReconnect();
    }
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer !== null || !this.playerKey || this.paused) return;
    const delay = this.reconnectDelayMs;
    this.reconnectDelayMs = Math.min(this.reconnectDelayMs * 2, 30000);
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (this.playerKey) this.connect(this.playerKey);
    }, delay);
  }

  private async handleResyncRequired(): Promise<void> {
    if (!this.playerKey) return;
    this.cursors.clear(this.playerKey);
    this.state.markAuthoritativeStateStale();
    this.refreshInProgress = true;
    try {
      await this.refreshAuthoritativeState?.();
    } finally {
      this.refreshInProgress = false;
      if (this.playerKey) this.connect(this.playerKey);
    }
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer !== null) clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;
  }
}

function isResumePage(value: unknown): value is {
  events: unknown[];
  next_cursor: string;
  high_watermark: string;
  has_more: boolean;
} {
  return isRecord(value)
    && Array.isArray(value['events'])
    && typeof value['next_cursor'] === 'string'
    && typeof value['high_watermark'] === 'string'
    && typeof value['has_more'] === 'boolean';
}

function isResyncRequired(value: unknown): boolean {
  if (!isRecord(value)) return false;
  const directData = value['data'];
  if (isRecord(directData) && directData['resync_required'] === true) return true;
  const wrapped = value['error'];
  if (isRecord(wrapped)) {
    const errorData = wrapped['data'];
    if (isRecord(errorData) && errorData['resync_required'] === true) return true;
  }
  return false;
}
