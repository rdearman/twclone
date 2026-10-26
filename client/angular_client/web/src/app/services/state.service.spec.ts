import { TestBed } from '@angular/core/testing';

import { StateService } from './state.service';

describe('StateService', () => {
  let service: StateService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(StateService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });

  it('keeps per-domain freshness separate and marks cached data stale on reconnect', () => {
    service.acceptPlayer({ id: 7, credits: 25 }, 100);
    service.acceptShip({ id: 8, shields: 50 }, 100);
    service.acceptSector({ sector_id: 9, ports: [{ id: 4, name: 'Port' }] }, 100);
    expect(service.selectPort(4)).toBeTrue();
    service.markDisconnected();

    const state = service.snapshot();
    expect(state.session.authentication).toBe('unknown');
    expect(state.session.connection).toBe('offline');
    expect(state.player.freshness).toBe('disconnected');
    expect(state.ship.freshness).toBe('disconnected');
    expect(state.sector.freshness).toBe('disconnected');
    expect(state.port.freshness).toBe('disconnected');
    expect(state.player.value?.['credits']).toBe(25);
  });

  it('derives selected port freshness from the sector listing and expires stale values', () => {
    service.markAuthenticated();
    service.acceptSector({ sector_id: 9, ports: [{ id: 4, name: 'Port' }] }, 100);
    expect(service.selectPort(4)).toBeTrue();
    expect(service.snapshot().port.receivedAt).toBe(100);
    service.markExpired(50, 151);
    expect(service.snapshot().sector.freshness).toBe('stale');
    expect(service.snapshot().port.freshness).toBe('stale');
    expect(service.selectPort(999)).toBeFalse();
  });

  it('deduplicates notices by event id and retains only a bounded history', () => {
    service.recordNotice({ id: 'evt-1', type: 'system.notice', data: { title: 'Test' }, cursor: 'c1' }, 100);
    service.recordNotice({ id: 'evt-1', type: 'system.notice', data: { title: 'Duplicate' } }, 101);
    for (let index = 0; index < 105; index++) {
      service.recordNotice({ id: `evt-${index + 2}`, type: 'chat.message' }, 200 + index);
    }
    const state = service.snapshot();
    expect(state.notices.length).toBe(100);
    expect(state.notices.some(notice => notice.id === 'evt-1')).toBeFalse();
    expect(state.lastReceivedEventCursor).toBe('c1');
  });

  it('ignores failed and malformed RPC responses instead of changing authority state', () => {
    service.updateFromRpc({ status: 'refused', data: { player: { credits: 0 } } });
    service.updateFromRpc({ status: 'ok', data: [] });
    expect(service.snapshot().player.freshness).toBe('unavailable');
    service.updateFromRpc({ status: 'ok', data: { player: { credits: 0 } } });
    expect(service.snapshot().player.value?.['credits']).toBe(0);
  });

  it('clears the previous captain state after confirmed logout', () => {
    service.markAuthenticated();
    service.acceptPlayer({ id: 7 }, 100);
    service.recordNotice({ id: 'n1', type: 'system.notice' }, 101);
    service.markUnauthenticated();
    const state = service.snapshot();
    expect(state.session.authentication).toBe('unauthenticated');
    expect(state.player.value).toBeNull();
    expect(state.notices).toEqual([]);
  });
});
