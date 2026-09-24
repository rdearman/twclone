import { Component } from '@angular/core';
import { CommonModule } from '@angular/common';
import { StateService } from '../../services/state.service';
import { TransportService } from '../../services/transport.service';

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
    private transport: TransportService
  ) {}

  get sector() {
    return this.state.gameState.sector;
  }

  move(sectorId: number) {
    this.transport.sendRpc('move.to', { sector_id: sectorId }).subscribe(res => {
        this.state.updateFromRpc(res);
    });
  }
}