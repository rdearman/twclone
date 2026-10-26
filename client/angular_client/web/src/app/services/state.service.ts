import { Injectable } from '@angular/core';
import { BehaviorSubject, Subject } from 'rxjs';

export type Freshness = 'unavailable' | 'refreshing' | 'fresh' | 'partial' | 'stale' | 'disconnected';
export type ConnectionState = 'offline' | 'connecting' | 'online' | 'reconnecting';
export type AuthenticationState = 'unknown' | 'authenticated' | 'unauthenticated';

export interface StateDomain<T> {
  value: T | null;
  freshness: Freshness;
  receivedAt: number | null;
}

export interface PlayerState { [key: string]: unknown; }
export interface ShipState { [key: string]: unknown; }
export interface SectorState {
  [key: string]: unknown;
  ports?: PortState[];
  planets?: unknown[];
  ships?: unknown[];
}
export interface PortState { [key: string]: unknown; }

export interface ClientNotice {
  id: string;
  type: string;
  data: unknown;
  receivedAt: number;
  cursor?: string;
}

export interface ClientEventRecord {
  id: string;
  type: string;
  data: unknown;
  receivedAt: number;
  cursor?: string;
}

export interface ClientStateSnapshot {
  session: {
    authentication: AuthenticationState;
    connection: ConnectionState;
    reconnectAttempt: number;
  };
  player: StateDomain<PlayerState>;
  ship: StateDomain<ShipState>;
  sector: StateDomain<SectorState>;
  /** Selected port is derived from the fresh sector listing, not a port API. */
  port: StateDomain<PortState>;
  events: ClientEventRecord[];
  notices: ClientNotice[];
  lastReceivedEventCursor: string | null;
}

function emptyDomain<T>(): StateDomain<T> {
  return { value: null, freshness: 'unavailable', receivedAt: null };
}

/**
 * Browser-side view state. Server responses remain authoritative; this service
 * only records their values and whether they are still safe to present as current.
 */
@Injectable({ providedIn: 'root' })
export class StateService {
  // Compatibility signals used by the older, inactive menu prototype.
  readonly lastRpcRequest$ = new BehaviorSubject<unknown>(null);
  readonly lastRpcResponse$ = new BehaviorSubject<unknown>(null);
  readonly prompt$ = new Subject<{ label: string; resolve: (value: string) => void }>();

  private state: ClientStateSnapshot = {
    session: { authentication: 'unknown', connection: 'offline', reconnectAttempt: 0 },
    player: emptyDomain<PlayerState>(),
    ship: emptyDomain<ShipState>(),
    sector: emptyDomain<SectorState>(),
    port: emptyDomain<PortState>(),
    events: [],
    notices: [],
    lastReceivedEventCursor: null
  };
  readonly snapshot$ = new BehaviorSubject<ClientStateSnapshot>(this.snapshot());
  private readonly seenEventIds = new Set<string>();
  private readonly seenEventCursors = new Set<string>();

  snapshot(): ClientStateSnapshot {
    return structuredClone(this.state);
  }

  markConnecting(reconnect = false): void {
    this.state.session.connection = reconnect ? 'reconnecting' : 'connecting';
    this.publish();
  }

  markAuthenticated(): void {
    this.state.session.authentication = 'authenticated';
    this.state.session.connection = 'online';
    this.state.session.reconnectAttempt = 0;
    this.markCachedDomains('stale');
    this.publish();
  }

  /** Updates stream connectivity without asserting anything about session validity. */
  markStreamConnected(): void {
    this.state.session.connection = 'online';
    this.state.session.reconnectAttempt = 0;
    this.publish();
  }

  markReconnectAttempt(): void {
    this.state.session.reconnectAttempt += 1;
    this.state.session.connection = 'reconnecting';
    this.markCachedDomains('stale');
    this.publish();
  }

  markDisconnected(): void {
    // A lost socket does not prove the server session was revoked or expired.
    this.state.session.authentication = 'unknown';
    this.state.session.connection = 'offline';
    this.markCachedDomains('disconnected');
    this.publish();
  }

  markUnauthenticated(): void {
    this.seenEventIds.clear();
    this.seenEventCursors.clear();
    this.state = {
      session: { authentication: 'unauthenticated', connection: 'offline', reconnectAttempt: 0 },
      player: emptyDomain<PlayerState>(),
      ship: emptyDomain<ShipState>(),
      sector: emptyDomain<SectorState>(),
      port: emptyDomain<PortState>(),
      events: [],
      notices: [],
      lastReceivedEventCursor: null
    };
    this.publish();
  }

  beginRefresh(domains: Array<'player' | 'ship' | 'sector'>): void {
    for (const name of domains) {
      const domain = this.state[name];
      domain.freshness = 'refreshing';
    }
    if (domains.includes('sector')) this.state.port.freshness = this.state.port.value ? 'stale' : 'unavailable';
    this.publish();
  }

  acceptPlayer(value: PlayerState, receivedAt = Date.now()): void {
    this.state.player = { value, freshness: 'fresh', receivedAt };
    this.publish();
  }

  acceptShip(value: ShipState, receivedAt = Date.now()): void {
    this.state.ship = { value, freshness: 'fresh', receivedAt };
    this.publish();
  }

  acceptSector(value: SectorState, receivedAt = Date.now()): void {
    this.state.sector = { value, freshness: 'fresh', receivedAt };
    this.state.port = { value: null, freshness: 'unavailable', receivedAt: null };
    this.publish();
  }

  acceptPort(value: PortState, receivedAt = Date.now()): void {
    this.state.port = { value, freshness: 'fresh', receivedAt };
    this.publish();
  }

  /** Consume only successful existing response envelopes; never invent values. */
  updateFromRpc(response: unknown): void {
    if (!response || typeof response !== 'object') return;
    const envelope = response as Record<string, unknown>;
    if (envelope['status'] !== 'ok' && envelope['ok'] !== true) return;
    const data = (envelope['data'] ?? (envelope['result'] as Record<string, unknown> | undefined)?.['data']);
    if (!data || typeof data !== 'object' || Array.isArray(data)) return;
    const payload = data as Record<string, unknown>;
    const receivedAt = Date.now();
    if (payload['player'] && typeof payload['player'] === 'object' && !Array.isArray(payload['player'])) {
      this.acceptPlayer(payload['player'] as PlayerState, receivedAt);
    }
    if (payload['ship'] && typeof payload['ship'] === 'object' && !Array.isArray(payload['ship'])) {
      this.acceptShip(payload['ship'] as ShipState, receivedAt);
    }
    if (payload['port'] && typeof payload['port'] === 'object' && !Array.isArray(payload['port'])) {
      this.acceptPort(payload['port'] as PortState, receivedAt);
    }
    if (typeof payload['sector_id'] === 'number' && Number.isInteger(payload['sector_id']) && Array.isArray(payload['ports'])) {
      this.acceptSector(payload as SectorState, receivedAt);
    }
  }

  selectPort(portId: unknown): boolean {
    const ports = this.state.sector.value?.ports ?? [];
    const found = ports.find(port => port['id'] === portId || port['port_id'] === portId);
    if (!found || this.state.sector.freshness !== 'fresh') {
      this.state.port = emptyDomain<PortState>();
      this.publish();
      return false;
    }
    this.state.port = {
      value: found,
      freshness: 'fresh',
      receivedAt: this.state.sector.receivedAt
    };
    this.publish();
    return true;
  }

  /** Records a validated server event. Event IDs and cursors are opaque strings. */
  recordEvent(event: { id: string; type: string; data: unknown; cursor?: string }, receivedAt = Date.now()): boolean {
    const type = event.type?.trim();
    if (!event.id || !type || this.seenEventIds.has(event.id)) return false;
    if (event.cursor && this.seenEventCursors.has(event.cursor)) return false;
    this.seenEventIds.add(event.id);
    if (event.cursor) this.seenEventCursors.add(event.cursor);
    this.state.events.push({ id: event.id, type, data: event.data ?? null, receivedAt, cursor: event.cursor });
    this.state.events = this.state.events.slice(-500);
    if (type.endsWith('.notice')) {
      this.state.notices.push({ id: event.id, type, data: event.data ?? null, receivedAt, cursor: event.cursor });
      this.state.notices = this.state.notices.slice(-100);
    }
    if (event.cursor) this.state.lastReceivedEventCursor = event.cursor;
    this.applyKnownEventInvalidation(type, event.data);
    this.trimSeen(this.seenEventIds, 2000);
    this.trimSeen(this.seenEventCursors, 2000);
    this.publish();
    return true;
  }

  /** Compatibility helper for older local notice callers without server envelopes. */
  recordNotice(event: { id?: string | number; type?: string; data?: unknown; cursor?: string | number }, receivedAt = Date.now()): void {
    const type = event.type?.trim();
    if (!type) return;
    const id = event.id === undefined ? `local-${receivedAt}-${this.state.notices.length}` : String(event.id);
    if (this.seenEventIds.has(id)) return;
    this.seenEventIds.add(id);
    const cursor = typeof event.cursor === 'string' ? event.cursor : undefined;
    if (cursor) {
      if (this.seenEventCursors.has(cursor)) return;
      this.seenEventCursors.add(cursor);
      this.state.lastReceivedEventCursor = cursor;
    }
    this.state.notices.push({ id, type, data: event.data ?? null, receivedAt, cursor });
    this.state.notices = this.state.notices.slice(-100);
    this.trimSeen(this.seenEventIds, 2000);
    this.trimSeen(this.seenEventCursors, 2000);
    this.publish();
  }

  markExpired(maxAgeMs: number, now = Date.now()): void {
    for (const domain of [this.state.player, this.state.ship, this.state.sector, this.state.port]) {
      if (domain.receivedAt !== null && now - domain.receivedAt > maxAgeMs && domain.freshness === 'fresh') {
        domain.freshness = 'stale';
      }
    }
    this.publish();
  }

  markAuthoritativeStateStale(): void {
    this.markCachedDomains('stale');
    this.publish();
  }

  /** Compatibility view for the older menu prototype; it is read-only derived state. */
  get gameState(): any {
    return {
      player: this.state.player.value ?? {},
      ship: this.state.ship.value ?? {},
      sector: this.state.sector.value ?? {},
      state: { freshness: this.freshnessSummary(), session: this.state.session }
    };
  }

  private markCachedDomains(freshness: Freshness): void {
    for (const domain of [this.state.player, this.state.ship, this.state.sector, this.state.port]) {
      domain.freshness = domain.value ? freshness : 'unavailable';
    }
  }

  private freshnessSummary(): Freshness {
    const domains = [this.state.player, this.state.ship, this.state.sector];
    if (domains.some(domain => domain.freshness === 'disconnected')) return 'disconnected';
    if (domains.some(domain => domain.freshness === 'refreshing')) return 'refreshing';
    if (domains.some(domain => domain.freshness === 'stale')) return 'stale';
    if (domains.some(domain => domain.freshness === 'fresh')) return 'fresh';
    return 'unavailable';
  }

  private publish(): void {
    this.snapshot$.next(this.snapshot());
  }

  private trimSeen(values: Set<string>, maxSize: number): void {
    while (values.size > maxSize) {
      const oldest = values.values().next().value as string | undefined;
      if (oldest === undefined) return;
      values.delete(oldest);
    }
  }

  private applyKnownEventInvalidation(type: string, data: unknown): void {
    // These documented movement events identify the affected sector, but do
    // not contain a full replacement sector snapshot. Mark cached contents
    // stale and let ordinary authoritative reads refresh them.
    if (type !== 'sector.player_entered' && type !== 'sector.player_left') return;
    if (!data || typeof data !== 'object' || Array.isArray(data)) return;
    const eventSector = (data as Record<string, unknown>)['sector_id'];
    const currentSector = this.state.sector.value?.['sector_id'];
    if (eventSector === currentSector && this.state.sector.value) {
      this.state.sector.freshness = 'stale';
      if (this.state.port.value) this.state.port.freshness = 'stale';
      this.publish();
    }
  }
}
