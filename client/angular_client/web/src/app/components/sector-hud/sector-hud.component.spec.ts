import { TestBed } from '@angular/core/testing';
import { of } from 'rxjs';
import { GameCommandTransport } from '../../services/transport.service';
import { SectorHudComponent } from './sector-hud.component';

class FakeCommands extends GameCommandTransport {
  sent: Array<{ command: string; data: unknown }> = [];
  override sendCommand(command: string, data: unknown = {}) {
    this.sent.push({ command, data });
    return of({ status: 'ok', data: { sector_id: 9, ports: [] } });
  }
}

describe('SectorHudComponent', () => {
  beforeEach(() => TestBed.configureTestingModule({
    imports: [SectorHudComponent],
    providers: [{ provide: GameCommandTransport, useClass: FakeCommands }]
  }));

  it('uses the supported move.warp contract for sector movement', () => {
    const fixture = TestBed.createComponent(SectorHudComponent);
    const commands = TestBed.inject(GameCommandTransport) as FakeCommands;
    fixture.componentInstance.move(9);
    expect(commands.sent).toEqual([{ command: 'move.warp', data: { to_sector_id: 9 } }]);
  });
});
