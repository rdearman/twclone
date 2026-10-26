import { Injectable } from '@angular/core';
import { HttpClient, HttpHeaders } from '@angular/common/http';
import { BehaviorSubject, Observable, Subject, map, of, switchMap, tap, throwError } from 'rxjs';
import type { ConnectionState } from './state.service';

/** The replayable event envelope documented in PROTOCOL.v3/30. */
export interface GameEventEnvelope {
  id: string;
  ts: string;
  type: string;
  data: unknown;
  meta: {
    topic: string;
    cursor?: string;
    replayable: boolean;
  };
}

export interface EventsResumeRequest {
  after_cursor?: string;
  through_cursor?: string;
  limit?: number;
}

export interface EventsResumeResponse {
  events: GameEventEnvelope[];
  next_cursor: string;
  high_watermark: string;
  has_more: boolean;
}

/** Platform-neutral command/event boundary for web and native wrappers. */
export abstract class ClientEventTransport {
  abstract readonly connectionState$: Observable<ConnectionState>;
  abstract readonly events$: Observable<unknown>;
  abstract readonly resyncRequired$: Observable<void>;
  configureGatewayOrigin?(origin: string): void;
  /** Implemented by a gateway transport once its authenticated stream route exists. */
  abstract connect(lastEventCursor?: string | null): void;
  abstract disconnect(): void;
  abstract sendCommand(command: string, data?: unknown, meta?: Record<string, unknown>): Observable<unknown>;
}

/** Authenticated same-origin WebSocket stream. The gateway resumes events.resume
 * before it begins live delivery, so this client only supplies the opaque cursor. */
@Injectable({ providedIn: 'root' })
export class GatewayEventTransport extends ClientEventTransport {
  private readonly connectionState = new BehaviorSubject<ConnectionState>('offline');
  private readonly events = new Subject<unknown>();
  private readonly resyncRequired = new Subject<void>();
  private socket: WebSocket | null = null;
  private generation = 0;
  private intentionalClose = false;
  private configuredOrigin = '';
  private readonly pending = new Map<string, { observer: { next(value: unknown): void; error(error: unknown): void; complete(): void }; timeout: ReturnType<typeof setTimeout> }>();

  override readonly connectionState$ = this.connectionState.asObservable();
  override readonly events$ = this.events.asObservable();
  override readonly resyncRequired$ = this.resyncRequired.asObservable();

  override configureGatewayOrigin(origin: string): void {
    this.configuredOrigin = origin.trim().replace(/\/$/, '');
  }

  override connect(lastEventCursor?: string | null): void {
    const previous = this.socket;
    const generation = ++this.generation;
    this.intentionalClose = false;
    this.socket = null;
    if (previous && previous.readyState < WebSocket.CLOSING) previous.close();
    this.connectionState.next('connecting');
    const base = this.configuredOrigin || globalThis.location?.href || 'http://localhost';
    const url = new URL('/api/v1/events', base);
    url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
    if (lastEventCursor) url.searchParams.set('after_cursor', lastEventCursor);

    try {
      const socket = new WebSocket(url.toString());
      this.socket = socket;
      socket.onopen = () => {
        if (generation === this.generation) this.connectionState.next('online');
      };
      socket.onmessage = message => {
        if (generation !== this.generation) return;
        let value: unknown;
        try { value = JSON.parse(String(message.data)); }
        catch { return; }
        if (isResyncNotice(value)) {
          this.resyncRequired.next();
          this.intentionalClose = true;
          socket.close(1000, 'Resynchronizing');
          return;
        }
        if (isCommandReply(value)) {
          const replyTo = value['reply_to'] as string;
          const pending = this.pending.get(replyTo);
          if (pending) {
            clearTimeout(pending.timeout);
            this.pending.delete(replyTo);
            pending.observer.next(value);
            pending.observer.complete();
            return;
          }
        }
        this.events.next(value);
      };
      socket.onerror = () => {
        if (generation === this.generation) this.connectionState.next('reconnecting');
      };
      socket.onclose = () => {
        if (generation === this.generation) {
          this.socket = null;
          this.failPendingCommands(new Error('Gateway event connection closed.'));
          this.connectionState.next(this.intentionalClose ? 'offline' : 'reconnecting');
        }
      };
    } catch {
      this.connectionState.next('reconnecting');
    }
  }

  override disconnect(): void {
    this.intentionalClose = true;
    this.generation += 1;
    const socket = this.socket;
    this.socket = null;
    this.failPendingCommands(new Error('Gateway event connection closed.'));
    if (socket && socket.readyState < WebSocket.CLOSING) socket.close();
    this.connectionState.next('offline');
  }

  override sendCommand(command: string, data: unknown = {}, meta: Record<string, unknown> = {}): Observable<unknown> {
    return new Observable(subscriber => {
      if (!command.trim()) {
        subscriber.error(new Error('A command name is required.'));
        return;
      }
      if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
        subscriber.error(new Error('The gateway event connection is not online.'));
        return;
      }
      const id = globalThis.crypto?.randomUUID?.() ?? `web-${Date.now()}-${Math.random().toString(16).slice(2)}`;
      const timeout = setTimeout(() => {
        const pending = this.pending.get(id);
        if (!pending) return;
        this.pending.delete(id);
        pending.observer.error(new Error('The game command timed out.'));
      }, 30000);
      this.pending.set(id, { observer: subscriber, timeout });
      try {
        this.socket.send(JSON.stringify({ id, command: command.trim(), data, meta }));
      } catch (error) {
        clearTimeout(timeout);
        this.pending.delete(id);
        subscriber.error(error);
      }
      return () => {
        const pending = this.pending.get(id);
        if (pending) clearTimeout(pending.timeout);
        this.pending.delete(id);
      };
    });
  }

  private failPendingCommands(error: Error): void {
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timeout);
      pending.observer.error(error);
    }
    this.pending.clear();
  }
}

function isCommandReply(value: unknown): value is Record<string, unknown> & { reply_to: string } {
  return !!value && typeof value === 'object' && !Array.isArray(value)
    && typeof (value as Record<string, unknown>)['reply_to'] === 'string';
}

function isResyncNotice(value: unknown): boolean {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const envelope = value as Record<string, unknown>;
  const data = envelope['data'];
  return envelope['type'] === 'gateway.events.resync_required'
    && !!data && typeof data === 'object' && !Array.isArray(data)
    && (data as Record<string, unknown>)['resync_required'] === true;
}

/** Resume command boundary. A gateway implementation can page events.resume here. */
export abstract class EventResumeTransport {
  abstract resumeEvents(request: EventsResumeRequest): Observable<EventsResumeResponse>;
}

/** Offline storage contract. Implementations must cache display state only, never queue mutations. */
export abstract class ClientSnapshotCache<T> {
  abstract load(): Promise<T | null>;
  abstract save(snapshot: T): Promise<void>;
  abstract clear(): Promise<void>;
}

export interface GatewayPlayerSummary {
  id: number;
  current_sector?: number;
  unread_news_count?: number;
}

export interface GatewayLoginResponse {
  player: GatewayPlayerSummary;
  csrf_token: string;
}

/** Browser-facing transport boundary. Game tokens never enter this interface. */
export abstract class GatewayTransport {
  abstract gatewayOrigin(): string;
  abstract configureGatewayOrigin(origin: string): void;
  abstract login(username: string, password: string): Observable<GatewayLoginResponse>;
  abstract logout(): Observable<void>;
  abstract playerMe(): Observable<{ player: unknown }>;
}

/** Existing player command boundary carried by the authenticated gateway socket. */
export abstract class GameCommandTransport {
  abstract sendCommand(command: string, data?: unknown): Observable<unknown>;
}

@Injectable({ providedIn: 'root' })
export class PendingGameCommandTransport extends GameCommandTransport {
  override sendCommand(): Observable<never> {
    return throwError(() => new Error('Game command stream is not available yet.'));
  }
}

@Injectable({ providedIn: 'root' })
export class GatewayGameCommandTransport extends GameCommandTransport {
  constructor(private readonly events: ClientEventTransport) { super(); }

  override sendCommand(command: string, data?: unknown): Observable<unknown> {
    return this.events.sendCommand(command, data ?? {});
  }
}

@Injectable({ providedIn: 'root' })
export class TransportService extends GatewayTransport {
  private readonly originStorageKey = 'twclone:gateway-origin';
  private csrfToken: string | null = null;

  constructor(private readonly http: HttpClient) {
    super();
  }

  override gatewayOrigin(): string {
    try { return globalThis.localStorage?.getItem(this.originStorageKey) ?? ''; }
    catch { return ''; }
  }

  override configureGatewayOrigin(origin: string): void {
    const value = origin.trim().replace(/\/$/, '');
    if (!value) {
      try { globalThis.localStorage?.removeItem(this.originStorageKey); } catch { /* storage optional */ }
      return;
    }
    let parsed: URL;
    try { parsed = new URL(value); }
    catch { throw new Error('Enter a valid gateway URL.'); }
    const privateHttp = parsed.protocol === 'http:' && isDevelopmentHost(parsed.hostname);
    if (parsed.username || parsed.password || parsed.pathname !== '/' || parsed.search || parsed.hash
      || (parsed.protocol !== 'https:' && !privateHttp)) {
      throw new Error('Use an HTTPS gateway URL. HTTP is allowed only for a private development host.');
    }
    try { globalThis.localStorage?.setItem(this.originStorageKey, parsed.origin); }
    catch { throw new Error('The gateway URL could not be saved on this device.'); }
  }

  private get apiBase(): string {
    const origin = this.gatewayOrigin();
    return origin ? `${origin}/api/v1` : '/api/v1';
  }

  override login(username: string, password: string): Observable<GatewayLoginResponse> {
    return this.http.post<GatewayLoginResponse>(
      `${this.apiBase}/auth/login`,
      { username, password },
      { withCredentials: true }
    ).pipe(tap(response => this.csrfToken = response.csrf_token));
  }

  override logout(): Observable<void> {
    const csrf$ = this.csrfToken
      ? of(this.csrfToken)
      : this.http.get<{ csrf_token: string }>(
          `${this.apiBase}/auth/csrf`, { withCredentials: true }
        ).pipe(tap(value => this.csrfToken = value.csrf_token), map(value => value.csrf_token));

    return csrf$.pipe(
      switchMap(token => this.http.post<void>(
        `${this.apiBase}/auth/logout`,
        {},
        {
          withCredentials: true,
          headers: new HttpHeaders({ 'X-CSRF-Token': token })
        }
      )),
      tap(() => this.csrfToken = null)
    );
  }

  override playerMe(): Observable<{ player: unknown }> {
    return this.http.get<{ player: unknown }>(
      `${this.apiBase}/player/me`, { withCredentials: true }
    );
  }
}

function isDevelopmentHost(host: string): boolean {
  if (host === 'localhost' || host === '127.0.0.1' || host === '::1') return true;
  const octets = host.split('.').map(Number);
  if (octets.length !== 4 || octets.some(value => !Number.isInteger(value) || value < 0 || value > 255)) return false;
  return octets[0] === 10
    || (octets[0] === 172 && octets[1] >= 16 && octets[1] <= 31)
    || (octets[0] === 192 && octets[1] === 168);
}
