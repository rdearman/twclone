import { BehaviorSubject, of, Subject, throwError } from 'rxjs';
import { ClientEventTransport, EventResumeTransport, EventsResumeRequest, EventsResumeResponse, GameEventEnvelope } from './transport.service';
import { EventBridgeCoordinator, EventCursorStore, parseGameEventEnvelope } from './event-bridge.service';
import { StateService, ConnectionState } from './state.service';

const eventOne: GameEventEnvelope = {
  id: '7f12c743-6fb9-4a6b-b9d9-7a9dc52d0157',
  ts: '2026-09-29T12:34:56Z',
  type: 'sector.player_entered',
  data: { v: 1, player_id: 42, sector_id: 18 },
  meta: { topic: 'sector.18', cursor: 'opaque-cursor-1', replayable: true }
};

class FakeEventTransport extends ClientEventTransport {
  readonly connectionState$ = new BehaviorSubject<ConnectionState>('offline');
  readonly events$ = new Subject<unknown>();
  readonly resyncRequired$ = new Subject<void>();
  connectedWith: Array<string | null | undefined> = [];
  disconnectCalls = 0;

  override connect(cursor?: string | null): void { this.connectedWith.push(cursor); }
  override disconnect(): void { this.disconnectCalls += 1; }
  override sendCommand() { return of({ status: 'ok' }); }
}

class MemoryCursorStore implements EventCursorStore {
  readonly values = new Map<string, string>();
  load(player: string): string | null { return this.values.get(player) ?? null; }
  save(player: string, cursor: string): void { this.values.set(player, cursor); }
  clear(player: string): void { this.values.delete(player); }
}

class FakeResumeTransport extends EventResumeTransport {
  readonly requests: EventsResumeRequest[] = [];
  constructor(private readonly pages: EventsResumeResponse[]) { super(); }
  override resumeEvents(request: EventsResumeRequest) {
    this.requests.push({ ...request });
    const page = this.pages[this.requests.length - 1];
    return of(page);
  }
}

describe('event bridge preparation', () => {
  it('validates replayable and transient envelopes according to the documented contract', () => {
    expect(parseGameEventEnvelope(eventOne)).toEqual(eventOne);
    expect(parseGameEventEnvelope({ ...eventOne, meta: { topic: 'sector.18', replayable: true } })).toBeNull();
    expect(parseGameEventEnvelope({ ...eventOne, meta: { topic: 'sector.18', cursor: 'c1', replayable: false } })).toBeNull();
    expect(parseGameEventEnvelope({ ...eventOne, id: 'bad-id' })).toBeNull();
    expect(parseGameEventEnvelope({ ...eventOne, ts: 'yesterday' })).toBeNull();
    expect(parseGameEventEnvelope({ ...eventOne, meta: { topic: 'presence', replayable: false } })).toEqual({
      ...eventOne,
      meta: { topic: 'presence', replayable: false }
    });
  });

  it('reconnects with the stored opaque cursor and persists each accepted replayable event', () => {
    const state = new StateService();
    const stream = new FakeEventTransport();
    const cursors = new MemoryCursorStore();
    const resume = new FakeResumeTransport([]);
    cursors.save('42', 'opaque-cursor-before-reconnect');
    const bridge = new EventBridgeCoordinator(state, stream, resume, cursors);

    bridge.connect(42);
    expect(stream.connectedWith).toEqual(['opaque-cursor-before-reconnect']);
    stream.events$.next(eventOne);
    stream.events$.next(eventOne);
    expect(cursors.load('42')).toBe('opaque-cursor-1');
    expect(state.snapshot().lastReceivedEventCursor).toBe('opaque-cursor-1');
    expect(state.snapshot().events.length).toBe(1);

    bridge.reconnect();
    expect(stream.connectedWith).toEqual(['opaque-cursor-before-reconnect', 'opaque-cursor-1']);
    expect(state.snapshot().session.reconnectAttempt).toBe(1);

    bridge.disconnect(true);
    expect(cursors.load('42')).toBeNull();
    bridge.destroy();
  });

  it('does not advance the resume cursor for non-replayable live events', () => {
    const state = new StateService();
    const stream = new FakeEventTransport();
    const cursors = new MemoryCursorStore();
    const bridge = new EventBridgeCoordinator(state, stream, new FakeResumeTransport([]), cursors);
    bridge.connect('42');
    stream.events$.next({
      ...eventOne,
      id: 'd82c7925-4ff6-48f1-9ec7-9987f74a10ea',
      type: 'presence.updated',
      meta: { topic: 'presence', replayable: false }
    });
    expect(state.snapshot().events.length).toBe(1);
    expect(state.snapshot().lastReceivedEventCursor).toBeNull();
    expect(cursors.load('42')).toBeNull();
    bridge.destroy();
  });

  it('marks affected sector contents stale for documented player movement events', () => {
    const state = new StateService();
    state.acceptSector({ sector_id: 18, ports: [{ port_id: 5 }] }, 100);
    state.selectPort(5);
    const stream = new FakeEventTransport();
    const bridge = new EventBridgeCoordinator(state, stream, new FakeResumeTransport([]), new MemoryCursorStore());
    bridge.connect(42);
    stream.events$.next({
      ...eventOne,
      type: 'sector.player_entered',
      data: { v: 1, player_id: 9, sector_id: 18 }
    });
    expect(state.snapshot().sector.freshness).toBe('stale');
    expect(state.snapshot().port.freshness).toBe('stale');
    bridge.destroy();
  });

  it('pages events.resume with the fixed high watermark and opaque next cursor', async () => {
    const secondEvent: GameEventEnvelope = {
      ...eventOne,
      id: 'd82c7925-4ff6-48f1-9ec7-9987f74a10ea',
      type: 'sector.notice',
      meta: { topic: 'sector.18', cursor: 'opaque-cursor-2', replayable: true }
    };
    const resume = new FakeResumeTransport([
      { events: [eventOne], next_cursor: 'opaque-cursor-1', high_watermark: 'opaque-cursor-2', has_more: true },
      { events: [secondEvent], next_cursor: 'opaque-cursor-2', high_watermark: 'opaque-cursor-2', has_more: false }
    ]);
    const state = new StateService();
    const cursors = new MemoryCursorStore();
    const bridge = new EventBridgeCoordinator(state, new FakeEventTransport(), resume, cursors);
    cursors.save('42', 'opaque-start');
    bridge.connect('42');

    expect(await bridge.resumeHistory(25)).toBe('complete');
    expect(resume.requests).toEqual([
      { after_cursor: 'opaque-start', limit: 25 },
      { after_cursor: 'opaque-cursor-1', through_cursor: 'opaque-cursor-2', limit: 25 }
    ]);
    expect(state.snapshot().events.map(event => event.id)).toEqual([eventOne.id, secondEvent.id]);
    expect(cursors.load('42')).toBe('opaque-cursor-2');
    bridge.destroy();
  });

  it('rejects a resume page that claims more data without advancing its cursor', async () => {
    const resume = new FakeResumeTransport([
      { events: [], next_cursor: 'same-cursor', high_watermark: 'high', has_more: true }
    ]);
    const cursors = new MemoryCursorStore();
    cursors.save('42', 'same-cursor');
    const bridge = new EventBridgeCoordinator(new StateService(), new FakeEventTransport(), resume, cursors);
    bridge.connect('42');
    expect(await bridge.resumeHistory()).toBe('invalid_page');
    bridge.destroy();
  });

  it('clears an expired cursor and marks cached state stale when the server requires resync', async () => {
    class InvalidCursorTransport extends EventResumeTransport {
      override resumeEvents() {
        return throwError(() => ({ data: { resync_required: true } }));
      }
    }
    const state = new StateService();
    state.acceptPlayer({ player_id: 42, credits: 10 });
    const cursors = new MemoryCursorStore();
    cursors.save('42', 'expired-cursor');
    const bridge = new EventBridgeCoordinator(state, new FakeEventTransport(), new InvalidCursorTransport(), cursors);
    bridge.connect('42');

    expect(await bridge.resumeHistory()).toBe('resync_required');
    expect(cursors.load('42')).toBeNull();
    expect(state.snapshot().player.freshness).toBe('stale');
    bridge.destroy();
  });

  it('refreshes authoritative state and reconnects without an expired cursor', async () => {
    const state = new StateService();
    state.acceptPlayer({ player_id: 42, credits: 10 });
    const stream = new FakeEventTransport();
    const cursors = new MemoryCursorStore();
    const refresh = jasmine.createSpy('refreshAuthoritativeState').and.resolveTo();
    cursors.save('42', 'expired-cursor');
    const bridge = new EventBridgeCoordinator(state, stream, null, cursors, refresh);
    bridge.connect(42);
    stream.resyncRequired$.next();
    await Promise.resolve();
    await Promise.resolve();

    expect(refresh).toHaveBeenCalledTimes(1);
    expect(cursors.load('42')).toBeNull();
    expect(stream.connectedWith).toEqual(['expired-cursor', null]);
    expect(state.snapshot().player.freshness).toBe('stale');
    bridge.disconnect();
    bridge.destroy();
  });
});
