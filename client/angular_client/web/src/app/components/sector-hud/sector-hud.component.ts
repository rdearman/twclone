import { Component } from '@angular/core';
import { CommonModule } from '@angular/common';
import { StateService } from '../../services/state.service';
import { GameCommandTransport } from '../../services/transport.service';

@Component({
  selector: 'app-sector-hud',
  standalone: true,
  imports: [CommonModule],
  templateUrl: './sector-hud.component.html',
  styleUrl: './sector-hud.component.scss'
})
export class SectorHudComponent {
  constructor(
    public state: StateService,
    private transport: GameCommandTransport
  ) {}

  get sector() {
    return this.state.gameState.sector;
  }

  move(sectorId: number) {
    this.transport.sendCommand('move.warp', { to_sector_id: sectorId }).subscribe(res => {
        this.state.updateFromRpc(res);
    });
  }
}
