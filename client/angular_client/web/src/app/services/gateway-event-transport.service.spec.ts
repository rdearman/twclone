import { GatewayEventTransport } from './transport.service';

describe('GatewayEventTransport', () => {
  const originalWebSocket = globalThis.WebSocket;
  let socket: FakeWebSocket;

  class FakeWebSocket {
    static readonly CLOSING = 2;
    static readonly CLOSED = 3;
    static readonly OPEN = 1;
    static readonly CONNECTING = 0;
    readyState = FakeWebSocket.CONNECTING;
    onopen: ((event: Event) => void) | null = null;
    onmessage: ((event: MessageEvent) => void) | null = null;
    onerror: ((event: Event) => void) | null = null;
    onclose: ((event: CloseEvent) => void) | null = null;
    url: string;
    sent: string[] = [];

    constructor(url: string) {
      this.url = url;
      socket = this;
    }

    close(): void {
      this.readyState = FakeWebSocket.CLOSED;
      this.onclose?.({} as CloseEvent);
    }

    send(value: string): void { this.sent.push(value); }
  }

  beforeEach(() => {
    Object.defineProperty(globalThis, 'WebSocket', { configurable: true, value: FakeWebSocket });
  });

  afterEach(() => {
    Object.defineProperty(globalThis, 'WebSocket', { configurable: true, value: originalWebSocket });
  });

  it('connects to the same-origin event endpoint and passes the opaque resume cursor', () => {
    const transport = new GatewayEventTransport();
    transport.connect('opaque/cursor + value');
    const url = new URL(socket.url);
    expect(url.pathname).toBe('/api/v1/events');
    expect(url.searchParams.get('after_cursor')).toBe('opaque/cursor + value');
    expect(url.protocol).toBe(globalThis.location.protocol === 'https:' ? 'wss:' : 'ws:');
    transport.disconnect();
  });

  it('uses the configured HTTPS gateway origin for a Capacitor WebView', () => {
    const transport = new GatewayEventTransport();
    transport.configureGatewayOrigin('https://game.example.test');
    transport.connect(null);
    const url = new URL(socket.url);
    expect(url.origin).toBe('wss://game.example.test');
    expect(url.pathname).toBe('/api/v1/events');
    transport.disconnect();
  });

  it('forwards event envelopes and reports an expired cursor separately', () => {
    const transport = new GatewayEventTransport();
    const events: unknown[] = [];
    let resyncs = 0;
    transport.events$.subscribe(value => events.push(value));
    transport.resyncRequired$.subscribe(() => resyncs++);
    transport.connect(null);
    socket.onmessage?.({ data: JSON.stringify({ id: 'event-1', type: 'sector.notice' }) } as MessageEvent);
    socket.onmessage?.({
      data: JSON.stringify({ type: 'gateway.events.resync_required', data: { resync_required: true } })
    } as MessageEvent);
    expect(events).toEqual([{ id: 'event-1', type: 'sector.notice' }]);
    expect(resyncs).toBe(1);
    transport.disconnect();
  });

  it('sends existing game command envelopes without browser-supplied auth and correlates replies', () => {
    const transport = new GatewayEventTransport();
    transport.connect(null);
    socket.readyState = FakeWebSocket.OPEN;
    let reply: unknown;
    transport.sendCommand('ship.status', {}).subscribe(value => reply = value);
    const request = JSON.parse(socket.sent[0]) as Record<string, unknown>;
    expect(request['command']).toBe('ship.status');
    expect(request['data']).toEqual({});
    expect(request['auth']).toBeUndefined();
    socket.onmessage?.({ data: JSON.stringify({ reply_to: request['id'], status: 'ok', data: { ship: { id: 5 } } }) } as MessageEvent);
    expect(reply).toEqual({ reply_to: request['id'], status: 'ok', data: { ship: { id: 5 } } });
    transport.disconnect();
  });
});
