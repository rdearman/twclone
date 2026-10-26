import { TestBed } from '@angular/core/testing';
import { of, throwError } from 'rxjs';
import { AppComponent } from './app.component';
import { GameCommandTransport, GatewayTransport } from './services/transport.service';
import { StateService } from './services/state.service';
import { Observable } from 'rxjs';

class FakeGatewayTransport extends GatewayTransport {
  override gatewayOrigin() { return ''; }
  override configureGatewayOrigin() {}
  override login() { return of({ player: { id: 17, current_sector: 2271 }, csrf_token: 'csrf' }); }
  override logout() { return of(void 0); }
  override playerMe(): Observable<{ player: unknown }> { return throwError(() => ({ status: 401 })); }
}

class FakeGameCommandTransport extends GameCommandTransport {
  calls: Array<{ command: string; data: unknown }> = [];
  response: unknown = { status: 'ok', data: { success: true } };
  override sendCommand(command: string, data: unknown = {}) {
    this.calls.push({ command, data });
    return of(this.response);
  }
}

describe('AppComponent', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [AppComponent],
      providers: [
        { provide: GatewayTransport, useClass: FakeGatewayTransport },
        { provide: GameCommandTransport, useClass: FakeGameCommandTransport }
      ]
    }).compileComponents();
  });

  it('shows the existing-account login form without connecting directly to the game server', () => {
    const fixture = TestBed.createComponent(AppComponent);
    fixture.detectChanges();
    const compiled = fixture.nativeElement as HTMLElement;
    expect(compiled.querySelector('h1')?.textContent).toContain('TWClone');
    expect(compiled.querySelector('#username')).toBeTruthy();
    expect(compiled.querySelector('#password')).toBeTruthy();
    expect(compiled.querySelector('input[placeholder="Host"]')).toBeNull();
    expect(compiled.querySelector('button')?.textContent).toContain('LOGIN');
  });

  it('logs in through the gateway and then loads server-authoritative player details', () => {
    const transport = TestBed.inject(GatewayTransport) as FakeGatewayTransport;
    spyOn(transport, 'playerMe').and.returnValue(of({ player: { player: { id: 17 } } }));
    const fixture = TestBed.createComponent(AppComponent);
    const component = fixture.componentInstance;
    component.username = 'captain';
    component.password = 'secret';
    component.login();
    fixture.detectChanges();
    expect(component.playerSummary?.id).toBe(17);
    expect(component.playerDetails).toEqual({ player: { id: 17 } });
    expect(TestBed.inject(StateService).snapshot().session.authentication).toBe('authenticated');
    expect(TestBed.inject(StateService).snapshot().player.value?.['id']).toBe(17);
  });

  it('sends a selected sector ship to the existing combat.attack command', () => {
    const fixture = TestBed.createComponent(AppComponent);
    const component = fixture.componentInstance;
    const commands = TestBed.inject(GameCommandTransport) as FakeGameCommandTransport;
    component.sectorDetails = { ships_present: [{ ship_id: 52, name: 'Raider' }] };
    TestBed.inject(StateService).acceptSector({ sector_id: 2271, ships_present: [{ ship_id: 52, name: 'Raider' }] });
    component.selectCombatTarget({ ship_id: 52, name: 'Raider' });
    component.attackTarget();
    expect(commands.calls[0]).toEqual({ command: 'combat.attack', data: { target_ship_id: 52 } });
    expect(component.commandError).toBe('');
  });

  it('does not offer the player ship as a combat target', () => {
    const fixture = TestBed.createComponent(AppComponent);
    const component = fixture.componentInstance;
    component.shipDetails = { ship_id: 41, name: 'Player ship' };
    TestBed.inject(StateService).acceptSector({ ships_present: [
      { ship_id: 41, name: 'Player ship' }, { ship_id: 52, name: 'Raider' }
    ] });
    expect(component.sectorShips.map(ship => ship['ship_id'])).toEqual([52]);
  });

  it('shows a server refusal from combat without treating it as a successful attack', () => {
    const fixture = TestBed.createComponent(AppComponent);
    const component = fixture.componentInstance;
    const commands = TestBed.inject(GameCommandTransport) as FakeGameCommandTransport;
    commands.response = { status: 'refused', error: { code: 402, message: 'Captain Z intervenes' } };
    component.targetShipId = '52';
    component.attackTarget();
    expect(component.commandError).toBe('Captain Z intervenes');
    expect(component.commandResult).toBeNull();
  });

  it('uses current sector and selected port in existing navigation and trade commands', () => {
    const fixture = TestBed.createComponent(AppComponent);
    const component = fixture.componentInstance;
    const commands = TestBed.inject(GameCommandTransport) as FakeGameCommandTransport;
    commands.response = { status: 'ok', data: { port: { port_id: 12, name: 'Market' } } };
    TestBed.inject(StateService).acceptSector({
      sector_id: 2271, ports: [{ port_id: 12 }], adjacent_sectors: [{ sector_id: 2273, name: 'Nearby' }]
    });
    component.selectPort({ port_id: 12, name: 'Market' });
    component.commodity = 'ORE';
    component.tradeQuantity = 3;
    component.quoteTrade();
    component.trade('trade.buy');
    component.destinationSector = '2272';
    component.warp();
    component.warpTo(2273);
    const portStatus = commands.calls.find(call => call.command === 'port.status');
    const quote = commands.calls.find(call => call.command === 'trade.quote');
    const purchase = commands.calls.find(call => call.command === 'trade.buy');
    expect(portStatus).toEqual({ command: 'port.status', data: { port_id: 12, sector_id: 2271 } });
    expect(quote).toEqual({ command: 'trade.quote', data: { port_id: 12, commodity: 'ORE', quantity: 3 } });
    expect(purchase?.data).toEqual(jasmine.objectContaining({
      port_id: 12, sector_id: 2271, account: 0, items: [{ commodity: 'ORE', quantity: 3 }]
    }));
    expect(commands.calls.some(call => call.command === 'move.warp' && (call.data as Record<string, unknown>)['to_sector_id'] === 2272)).toBeTrue();
    expect(commands.calls.some(call => call.command === 'move.warp' && (call.data as Record<string, unknown>)['to_sector_id'] === 2273)).toBeTrue();
  });
});
